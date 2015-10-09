
/*hello_world.c
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
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>


#define BACNET_INSTANCE_NO          30     // assigned register at VU
//#define BACNET_INSTANCE_NO          12  // for testing at home
#define INITIAL_TESTING             0     // the initial testing used an array of 'magic' numbers, should be disabled when using modbus input


#define BACNET_PORT                 0xBAC1

#define BACNET_INTERFACE            "lo"
#define BACNET_DATALINK_TYPE        "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1       /* ms */


#define RUN_AS_BBMD_CLIENT          1


#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT            0xBAC0      //BBMD broadcast management device
//#define BACNET_BBMD_ADDRESS         "127.255.255.255"

/* if using bacnet_client Testbench on laptop */
//#define BACNET_BBMD_ADDRESS         "127.0.0.1" 
/* if using bacnet_client at VU */
#define BACNET_BBMD_ADDRESS         "140.159.160.7" 

#define BACNET_BBMD_TTL             90          //BBMD broadcast management device time to live
#endif

/* if using modbus_server Testbench on laptop */
//#define MODBUS_IP_ADDRESS           "127.0.0.1"
/* if using modbus_server at VU */
#define MODBUS_IP_ADDRESS           "140.159.153.159"





uint16_t holding[3]; // the variable that is passed from the thread 
static uint16_t tab_reg[3] = {};
int errno;
int i;
int rc;
modbus_t *ctx;


/* node structure used for the linked list */
typedef struct s_word_object word_object;
struct s_word_object {
        uint16_t word;
	word_object *next;
};	


#define NUM_CHANNELS 3
static word_object *list_heads[NUM_CHANNELS];
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

static void add_to_list(word_object **list_head, uint16_t word) {
	printf("ADD TO LIST\n");
	word_object *last_object, *tmp_object;
 	int16_t tmp_string;
 	tmp_object = malloc(sizeof(word_object)); //create the node tmp object
 	tmp_string = word;    //move modbus data to tmp_string
 	tmp_object->word = tmp_string; //move the modbus data to the tmp object node
  	tmp_object->next = NULL; //make the tmp the last object
	pthread_mutex_lock(&list_lock);
	if (*list_head == NULL) { //if the list head is null then the list head is the tmp object
		*list_head = tmp_object;
	} 
	else{
/* Iterate through the linked list to find the last object */
		last_object = *list_head;
		while (last_object->next){
			last_object = last_object->next;
		}
/* Last object is now found, link in our tmp_object at the tail */
		last_object->next = tmp_object;
	}
	pthread_mutex_unlock(&list_lock);
	pthread_cond_signal(&list_data_ready);
}

static word_object *list_get_first(word_object **list_head) {
	word_object *first_object;
	first_object = *list_head;
	*list_head = (*list_head)->next;
	return first_object;
}

 
static void list_flush(word_object *list_head) {
	pthread_mutex_lock(&list_lock);
	while (list_head != NULL) {
		pthread_cond_signal(&list_data_ready);
		pthread_cond_wait(&list_data_flush, &list_lock);
	}
	pthread_mutex_unlock(&list_lock);
}

/* If you are trying out the test suite from home, this data matches the data
 * stored in RANDOM_DATA_POOL for device number 12
 * BACnet client will print "Successful match" whenever it is able to receive
 * this set of data. Note that you will not have access to the RANDOM_DATA_POOL
 * for your final submitted application. */

#if INITIAL_TESTING
/* Only needed for initial testing! */

static uint16_t test_data[] = {
    0xA4EC, 0x6E39, 0x8740, 0x1065, 0x9134, 0xFC8C };
#define NUM_TEST_DATA (sizeof(test_data)/sizeof(test_data[0]))

#endif

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;


static int Update_Analog_Input_Read_Property(BACNET_READ_PROPERTY_DATA *rpdata) {

#if INITIAL_TESTING
	static int index;    /* only need for initial testing */
#endif
	word_object *current_object_0;
	int instance = Analog_Input_Instance_To_Index(rpdata->object_instance);
	
	if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) goto not_pv;
	pthread_mutex_lock(&list_lock);
	if(list_heads[instance] == NULL){
		pthread_mutex_unlock(&list_lock);
		goto not_pv;
	}
	current_object_0 = list_get_first(&list_heads[instance]);
	pthread_mutex_unlock(&list_lock);
	holding[instance] =  current_object_0->word;
	free(current_object_0);
	bacnet_Analog_Input_Present_Value_Set(instance, holding[instance]);
	
	printf("HOLDING: %d INSTANCE: %d\n" , holding[instance],instance); //for diagnostics 
    		

    /* Update the values to be sent to the BACnet client here.
     * The data should be read from the tail of a linked list. You are required
     * to implement this list functionality.
     *
     * bacnet_Analog_Input_Present_Value_Set() 
     *     First argument: Instance No
     *     Second argument: data to be sent
     *
     * Without reconfiguring libbacnet, a maximum of 4 values may be sent */
       	
#if INITIAL_TESTING	
	bacnet_Analog_Input_Present_Value_Set(0, test_data[index++]);

     	if (index == NUM_TEST_DATA) index = 0;
#endif

	not_pv:
    	return bacnet_Analog_Input_Read_Property(rpdata);
}



static bacnet_object_functions_t server_objects[] = {
    {bacnet_OBJECT_DEVICE,
	    NULL,
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
            Update_Analog_Input_Read_Property,
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

static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
    /* Thread safety: Shares data with datalink_send_pdu */
    bacnet_bvlc_register_with_bbmd(
	    bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS), 
	    htons(BACNET_BBMD_PORT),
	    BACNET_BBMD_TTL);
#endif
}

static void *minute_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);

	/* Re-register with BBMD once BBMD TTL has expired */
	register_with_bbmd();

	/* Update addresses for notification class recipient list 
	 * Requred for INTRINSIC_REPORTING
	 * bacnet_Notification_Class_find_recipient(); */
	
	/* Sleep for 1 minute */
	pthread_mutex_unlock(&timer_lock);
	sleep(60);
    }
    return arg;
}

static void *second_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);

	/* Transaction state machine: Responsible for retransmissions and ack
	 * checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);

	/* Re-enables communications after DCC_Time_Duration_Seconds
	 * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
	 * bacnet_dcc_timer_seconds(1); */

	/* State machine for load control object
	 * Required for OBJECT_LOAD_CONTROL
	 * bacnet_Load_Control_State_Machine_Handler(); */

	/* Expires any COV subscribers that have finite lifetimes
	 * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
	 * bacnet_handler_cov_timer_seconds(1); */

	/* Monitor Trend Log uLogIntervals and fetch properties
	 * Required for OBJECT_TRENDLOG
	 * bacnet_trend_log_timer(1); */
	
	/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
	 * Required for INTRINSIC_REPORTING
	 * bacnet_Device_local_reporting(); */
	
	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
    }
    return arg;
}

static void *modbus_func(void *arg) {
	
	modbus_start:
	printf("ONLY HERE AT START MODBUS\n");
	//ctx = modbus_new_tcp("140.159.153.159", MODBUS_TCP_DEFAULT_PORT); //using VU modbus
	ctx = modbus_new_tcp(MODBUS_IP_ADDRESS , MODBUS_TCP_DEFAULT_PORT);// using testbench from home
	//ctx = modbus_new_tcp("127.0.0.1", 0xBAC0); //old testbench
	if (modbus_connect(ctx) == -1){
		fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		sleep(1);
		goto modbus_start;
		//return -1;  //causing warnings *****
	}
	printf("conection not failed\n");
	//rc = modbus_read_registers(ctx, 0, 33, tab_reg);
	//rc = modbus_read_registers(ctx, 0, 3, tab_reg); // was used for initial modbus testbench
	if (rc == -1){
		if (EMBMDATA==1){printf("too many requests\n");}
		printf("not able to read register\n");
		fprintf(stderr, "%s\n", modbus_strerror(errno));
				
		//return -1;   // causing warnings *****
	}
	while(1){
		rc = modbus_read_registers(ctx, 30, 3, tab_reg);
		for (i=0; i < rc; i++){
			//sleep(1);
			printf("rc = %d\n",rc);
			printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
			add_to_list(&list_heads[i], tab_reg[i]);

		
		}
		usleep(100000);
		
		/* check modbus connect if failed re-establish connection */
		if (rc == -1){
			fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
			modbus_free(ctx);
			sleep(1);
			goto modbus_start;
		}
 
	}
}


static void ms_tick(void) {
    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */
}


#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)

int main(int argc, char **argv) {
	
	char input_word[256];
	int c;
	int option_index = 0;
	int count = -1;
	pthread_t print_thread;
    	pthread_t modbus0;
	uint8_t rx_buf[bacnet_MAX_MPDU];
    	uint16_t pdu_len;
    	BACNET_ADDRESS src;
    	pthread_t minute_tick_id, second_tick_id;

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

    	bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

    	pthread_create(&minute_tick_id, 0, minute_tick, NULL);
    	pthread_create(&second_tick_id, 0, second_tick, NULL);
    	pthread_create(&modbus0, 0, modbus_func, NULL);
		


    /* Start another thread here to retrieve your allocated registers from the
     * modbus server. This thread should have the following structure (in a
     * separate function):
     *
     * Initialise:
     *	    Connect to the modbus server
     *
     * Loop:
     *	    Read the required number of registers from the modbus server
     *	    Store the register data into a linked list 
     */

         	while (1) {
		pdu_len = bacnet_datalink_receive(
		    &src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);

		if (pdu_len) {
	    	/* May call any registered handler.
	     	* Thread safety: May block, however we still need to guarantee
	     	* atomicity with the timers, so hold the lock anyway */
	    		pthread_mutex_lock(&timer_lock);
			/* bacnet_npdu_handler calls Update_Analog_Input_Read_Property */
	    		bacnet_npdu_handler(&src, rx_buf, pdu_len);
	    		pthread_mutex_unlock(&timer_lock);
	
		}
 		//add_to_list(tab_reg[0]);
		ms_tick();

	 }


    return 0;
}






