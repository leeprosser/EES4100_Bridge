
/*_world.c
 * * Copyright (c) by Lee Prosser
 * * * Kim Taylor
 * * * Stéphane Raimbault
 **        <stephane.raimbault@gmail.com[1]>
 **
 ** * This code is based on the code from Libmodbus Manual.
 ** *
 ** * This program is free software; you can redistribute it and/or modify
 ** * it under the terms of the GNU General Public License as published by
 ** * the Free Software Foundation; either version 2 of the License, or
 ** * (at your option) any later version.
 ** *
 ** * This program is distributed in the hope that it will be useful,
 ** * but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 ** * GNU General Public License for more details.
 ** *
 ** * You should have received a copy of the GNU General Public License
 ** * along with this program; if not, write to the Free Software
 ** * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 ** *
 ** * 
 ** */

#include <stdio.h>
#include <modbus-tcp.h>
#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"
#include <modbus-tcp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <modbus-tcp.h>

#define BACNET_INSTANCE_NO          120
#define BACNET_PORT                 0xBAC1
#define BACNET_INTERFACE            "lo"
#define BACNET_DATALINK_TYPE        "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1       /* ms */

#define RUN_AS_BBMD_CLIENT          1

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT            0xBAC0      //BBMD broadcast management device
#define BACNET_BBMD_ADDRESS         "127.255.255.255"
#define BACNET_BBMD_TTL             90          //BBMD broadcast management device time to live
#endif

int16_t holding; // the variable that is passed from the thread 
//int16_t tab_reg[64]; // was used in modbus testbench
static uint16_t tab_reg[3] = {};
int errno;
int i;
int rc;

typedef struct s_word_object word_object;
struct s_word_object {
 // was ok int16_t *word;
  	int16_t word;
 	word_object *next;
};
 
 
  /* list_head: Shared between two threads, must be accessed with list_lock */
 
static word_object *list_head;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

static void add_to_list(int16_t word) {
	word_object *last_object, *tmp_object;
 	//word_object last_object, tmp_object;
 	int16_t tmp_string;
 	tmp_object = malloc(sizeof(word_object));
 	tmp_string = word;
 	tmp_object->word = tmp_string;
  	tmp_object->next = NULL;
	pthread_mutex_lock(&list_lock);
	if (list_head == NULL) {
		list_head = tmp_object;
 	} 
	else{
/* Iterate through the linked list to find the last object */
		last_object = list_head;
		while (last_object->next){
			last_object = last_object->next;
		}
/* Last object is now found, link in our tmp_object at the tail */
		last_object->next = tmp_object;
	}
	pthread_mutex_unlock(&list_lock);
	pthread_cond_signal(&list_data_ready);
}

static word_object *list_get_first(void) {
	word_object *first_object;
	first_object = list_head;
	list_head = list_head->next;
	return first_object;
}

static void *print_func(void *arg) {
	//static int holding;
	word_object *current_object;
	printf("in print func\n");
	fprintf(stderr, "Print thr starting\n");
	while(1) {
		pthread_mutex_lock(&list_lock);
		while (list_head == NULL) {
			pthread_cond_wait(&list_data_ready, &list_lock);
		}
		current_object = list_get_first();
		pthread_mutex_unlock(&list_lock);
		printf("Print thread: %d\n", current_object->word);
		holding =  current_object->word;  // the thread is passed to bacnet server via 'holding' variable
		printf("holding in function %d\n" , holding); // for diagnostics
		free(current_object);
		printf("in thread %d\n",i);
		pthread_cond_signal(&list_data_flush);
		//break;
	}
	printf("in thread\n"); //for diagnostics
	//return arg;
}

static void list_flush(void) {
	pthread_mutex_lock(&list_lock);
	while (list_head != NULL) {
		pthread_cond_signal(&list_data_ready);
		pthread_cond_wait(&list_data_flush, &list_lock);
	}
	pthread_mutex_unlock(&list_lock);
}
#define INC_TIMER(reset, func)  \
do {                            \
if (!--timer) {                 \
timer = reset;                  \
func();                         \
}                               \
} while (0)

static bacnet_object_functions_t server_objects[] = {
{bacnet_OBJECT_DEVICE, NULL,
  bacnet_Device_Count,
  bacnet_Device_Index_To_Instance,
  bacnet_Device_Valid_Object_Instance_Number,
  bacnet_Device_Object_Name,
  bacnet_Device_Read_Property_Local,
  bacnet_Device_Write_Property_Local,
  bacnet_Device_Property_Lists,
  bacnet_DeviceGetRRInfo,
  NULL, /* Iterator */
  NULL, /* Value_Lists */
  NULL, /* COV */
  NULL, /* COV Clear */
  NULL  /* Intrinsic Reporting */
},

{bacnet_OBJECT_ANALOG_INPUT,
  bacnet_Analog_Input_Init,
  bacnet_Analog_Input_Count,
  bacnet_Analog_Input_Index_To_Instance,
  bacnet_Analog_Input_Valid_Instance,
  bacnet_Analog_Input_Object_Name,
  bacnet_Analog_Input_Read_Property,
  bacnet_Analog_Input_Write_Property,
  bacnet_Analog_Input_Property_Lists,
  NULL /* ReadRangeInfo */ ,
  NULL /* Iterator */ ,
  bacnet_Analog_Input_Encode_Value_List,
  bacnet_Analog_Input_Change_Of_Value,
  bacnet_Analog_Input_Change_Of_Value_Clear,
  bacnet_Analog_Input_Intrinsic_Reporting},
  {MAX_BACNET_OBJECT_TYPE}
};


//int16_t tab_reg[64]; // was used in modbus testbench
int errno;
int i;
int rc;
/*int modb(void){
	printf("in function modbus\n");
	modbus_t *ctx;
	sleep(1);
	ctx = modbus_new_tcp("127.0.0.1", 0xBAC0);
	if (modbus_connect(ctx) == -1){
		fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		return -1;
	}
	printf("conection not failed\n");
	rc = modbus_read_registers(ctx, 0, 3, tab_reg);
	if (rc == -1){
		if (EMBMDATA==1){printf("too many requests\n");}
		printf("not able to read register\n");
		fprintf(stderr, "%s\n", modbus_strerror(errno));
		return -1;
	}
	for (i=0; i < rc; i++){
		printf("rc = %d\n",rc);
		printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
	}
	fflush(stdout);
	modbus_close(ctx);
	modbus_free(ctx);
}
*/
static void register_with_bbmd(void) {
	#if RUN_AS_BBMD_CLIENT
	bacnet_bvlc_register_with_bbmd(
	bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS),
	htons(BACNET_BBMD_PORT),
	BACNET_BBMD_TTL);
	#endif
}

static void minute_tick(void) {
/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);
/* Re-register with BBMD once BBMD TTL has expired */
        register_with_bbmd();
/* Update addresses for notification class recipient list 
 *          * *      * Requred for INTRINSIC_REPORTING
 *                   *           * bacnet_Notification_Class_find_recipient(); */
}

#define S_PER_MIN 60

static void second_tick(void) {
	static int timer = S_PER_MIN;
/* Invalidates stale BBMD foreign device table entries */
      	bacnet_bvlc_maintenance_timer(1);
/* Transaction state machine: Responsible for retransmissions and ack
 *         *      * checking for confirmed services */
        bacnet_tsm_timer_milliseconds(1000);
/* Re-enables communications after DCC_Time_Duration_Seconds
*         *      * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
*                 *           * bacnet_dcc_timer_seconds(1); */
 /* State machine for load control object
*         *      * Required for OBJECT_LOAD_CONTROL
*                 *           * bacnet_Load_Control_State_Machine_Handler(); */
 /* Expires any COV subscribers that have finite lifetimes
 *      * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
*                 *           * bacnet_handler_cov_timer_seconds(1); */
/* Monitor Trend Log uLogIntervals and fetch properties
*         *      * Required for OBJECT_TRENDLOG
*                 *           * bacnet_trend_log_timer(1); */
/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
*         *      * Required for INTRINSIC_REPORTING
*                 *           * bacnet_Device_local_reporting(); */
	INC_TIMER(S_PER_MIN, minute_tick);
}

#define MS_PER_SECOND 1000

static void ms_tick(void) {
        static int timer = MS_PER_SECOND;
 /* Updates change of value COV subscribers.
*** Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
*** bacnet_handler_cov_task(); */
        INC_TIMER(MS_PER_SECOND, second_tick);
}

#define BN_UNC(service, handler) \
bacnet_apdu_set_unconfirmed_handler(                \
SERVICE_UNCONFIRMED_##service,      \
bacnet_handler_##handler)
#define BN_CON(service, handler) \
bacnet_apdu_set_confirmed_handler(                  \
SERVICE_CONFIRMED_##service,        \
bacnet_handler_##handler)

int main(int argc, char **argv) {
        printf("in function main\n"); //for diagnostics
	char input_word[256];
	int c;
	int option_index = 0;
	int count = -1;
	pthread_t print_thread;
 /*static struct option long_options[] = {
  * {"count", required_argument, 0, 'c'},
  * {0, 0, 0, 0 }
  *  };
  * while (1) {
  * c = getopt_long(argc, argv, "c:", long_options, &option_index);
  *if (c == -1)
  *break;
  *switch (c) {
  *case 'c':
  *count = atoi(optarg);
  *break;
  * }
  * }*/

	uint8_t rx_buf[bacnet_MAX_MPDU];
        uint16_t pdu_len;
	BACNET_ADDRESS src;
	//modb();
	bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
	bacnet_address_init();
	   /* Setup device objects */
	bacnet_Device_Init(server_objects);
	BN_UNC(WHO_IS, who_is);
	BN_CON(READ_PROPERTY, read_property);
	bacnet_BIP_Debug = true;
	bacnet_bip_set_port(htons(BACNET_PORT));
	bacnet_datalink_set(BACNET_DATALINK_TYPE);
	bacnet_datalink_init(BACNET_INTERFACE);
	atexit(bacnet_datalink_cleanup);
	memset(&src, 0, sizeof(src));
	register_with_bbmd();
	bacnet_Send_I_Am(&bacnet_Handler_Transmit_Buffer[0]);
	//      while(1){modb();sleep(5); break;}
	//bacnet_Analog_Input_Present_Value_Set(0, holding);
	printf("before while ever func main\n");
	
	pthread_create(&print_thread, NULL, print_func, NULL);


	printf("in function modbus\n");
	modbus_t *ctx;
	//sleep(.1);
	//ctx = modbus_new_tcp("140.159.153.159", MODBUS_TCP_DEFAULT_PORT); //using VU modbus
	ctx = modbus_new_tcp("127.0.0.1", MODBUS_TCP_DEFAULT_PORT);
	//ctx = modbus_new_tcp("127.0.0.1", 0xBAC0); //using testbench modbus
	if (modbus_connect(ctx) == -1){
		fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		return -1;
	}
	printf("conection not failed\n");
	//rc = modbus_read_registers(ctx, 0, 33, tab_reg);
	//rc = modbus_read_registers(ctx, 0, 3, tab_reg); // was used for initial modbus testbench
	if (rc == -1){
		if (EMBMDATA==1){printf("too many requests\n");}
		printf("not able to read register\n");
		fprintf(stderr, "%s\n", modbus_strerror(errno));
		return -1;
	}

	while (1) {

        /*	printf("in function modbus\n");
		modbus_t *ctx;
		sleep(1);
		ctx = modbus_new_tcp("127.0.0.1", 0xBAC0);
		if (modbus_connect(ctx) == -1){
			fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
			modbus_free(ctx);
			return -1;
	  	}
	 	printf("conection not failed\n");
		rc = modbus_read_registers(ctx, 0, 3, tab_reg);
		if (rc == -1){
			if (EMBMDATA==1){printf("too many requests\n");}
			printf("not able to read register\n");
			fprintf(stderr, "%s\n", modbus_strerror(errno));
			return -1;
		}*/
		rc = modbus_read_registers(ctx, 30, 3, tab_reg);
		//rc = modbus_read_registers(ctx, 0, 3, tab_reg); // was used for testbench modbus server
		for (i=0; i < rc; i++){
			sleep(1);
			printf("rc = %d\n",rc);
			printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
	 		//add_to_list(tab_reg[30]);
	  		//add_to_list(tab_reg[i]); // was used for testbench modbus server
	 	
			//fflush(stdout);
			//modbus_close(ctx);
			//modbus_free(ctx);                    

			pdu_len = bacnet_datalink_receive(
			&src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);
			if (pdu_len) bacnet_npdu_handler(&src, rx_buf, pdu_len);
			ms_tick();
//			modb();
			printf("register number %d\n",i);
			//add_to_list(tab_reg[1]);
			printf("holding before bacnet number %d\n",holding);
			//pthread_create(&print_thread, NULL, print_func, NULL);
			printf("holding before bacnet number B  %d\n",holding);
			bacnet_Analog_Input_Present_Value_Set(0, holding); //from thread list
                	bacnet_Analog_Input_Present_Value_Set(1, 2.1); 
		//	bacnet_Analog_Input_Present_Value_Set(2, holding); 
			add_to_list(tab_reg[i]);
          	//      pthread_create(&print_thread, NULL, print_func, NULL);
          	//      bacnet_Analog_Input_Present_Value_Set(0, holding);
                	printf("holding %d\n", holding);
                 	//****    causing the program to stop!!!  
			//list_flush();
                	//add_to_list(tab_reg[0]);
               		//add_to_list(tab_reg[0]);
                	printf("after list flush\n");
                	//add_to_list(tab_reg[1]);
                	//add_to_list(tab_reg[2]);
        	}
		fflush(stdout);
	//	modbus_close(ctx);
	//	modbus_free(ctx);

	}
        return 0;
}

