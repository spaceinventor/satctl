/*
 * Copyright (c) 2014 Satlab ApS <satlab@satlab.com>
 * Proprietary software - All rights reserved.
 *
 * Satellite and subsystem control program
 */
//#include <style.css>
#include <cairo.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <poll.h>
#include <pigpio.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <slash/slash.h>

#include <param/param.h>
#include <vmem/vmem_server.h>
#include <vmem/vmem_ram.h>

#include <csp/csp.h>
#include <csp/arch/csp_thread.h>
#include <csp/interfaces/csp_if_can.h>
#include <csp/interfaces/csp_if_kiss.h>
#include <csp/drivers/usart.h>
#include <csp/drivers/can_socketcan.h>
#include <param/param_list.h>
#include <param/param_group.h>
#include <param/param_server.h>

#define SATCTL_PROMPT_GOOD		"\033[96msatctl \033[90m%\033[0m "
#define SATCTL_PROMPT_BAD		"\033[96msatctl \033[31m!\033[0m "
#define SATCTL_DEFAULT_INTERFACE	"can0"
#define SATCTL_DEFAULT_ADDRESS		0

#define SATCTL_LINE_SIZE		128
#define SATCTL_HISTORY_SIZE		2048

VMEM_DEFINE_STATIC_RAM(test, "test", 100000);
//--- Mihaela ---//
void showPinLevel(void);
//___END___//

void usage(void)
{
	printf("usage: satctl [command]\n");
	printf("\n");
	printf("Copyright (c) 2018 Space Inventor ApS <info@spaceinventor.com>\n");
	printf("Copyright (c) 2014 Satlab ApS <satlab@satlab.com>\n");
	printf("\n");
	printf("Options:\n");
	printf(" -i INTERFACE,\tUse INTERFACE as CAN interface\n");
	printf(" -n NODE\tUse NODE as own CSP address\n");
	printf(" -r REMOTE NODE\tUse NODE as remote CSP address for rparam\n");
	printf(" -h,\t\tPrint this help and exit\n");
}

int configure_csp(uint8_t addr, char *ifc)
{

	if (csp_buffer_init(100, 320) < 0)
		return -1;

	csp_conf_t csp_config;
	csp_conf_get_defaults(&csp_config);
	csp_config.address = addr;
	csp_config.hostname = "satctl";
	csp_config.model = "linux";
	if (csp_init(&csp_config) < 0)
		return -1;

	//csp_debug_set_level(4, 1);
	//csp_debug_set_level(5, 1);

#if 0
	struct usart_conf usart_conf = {
			.device = "/dev/ttyUSB0",
			.baudrate = 38400,
	};
	usart_init(&usart_conf);

	static csp_iface_t kiss_if;
	static csp_kiss_handle_t kiss_handle;
	void kiss_usart_putchar(char c) {
		usleep(4000);
		usart_putc(c);
	}
	void kiss_usart_callback(uint8_t *buf, int len, void *pxTaskWoken) {
		csp_kiss_rx(&kiss_if, buf, len, pxTaskWoken);
	}
	usart_set_callback(kiss_usart_callback);
	csp_kiss_init(&kiss_if, &kiss_handle, kiss_usart_putchar, NULL, "KISS");
#endif

	csp_iface_t *can0 = csp_can_socketcan_init(ifc, 1000000, 0);

	if (can0) {
		if (csp_route_set(CSP_DEFAULT_ROUTE, can0, CSP_NODE_MAC) < 0)
			return -1;
	}

	if (csp_route_start_task(0, 0) < 0)
		return -1;


	csp_rdp_set_opt(3, 10000, 5000, 1, 2000, 2);
	//csp_rdp_set_opt(10, 20000, 8000, 1, 5000, 9);

	csp_socket_t *sock_csh = csp_socket(CSP_SO_NONE);
	csp_socket_set_callback(sock_csh, csp_service_handler);
	csp_bind(sock_csh, CSP_ANY);

	csp_socket_t *sock_param = csp_socket(CSP_SO_NONE);
	csp_socket_set_callback(sock_param, param_serve);
	csp_bind(sock_param, PARAM_PORT_SERVER);

	csp_thread_handle_t vmem_handle;
	csp_thread_create(vmem_server_task, "vmem", 2000, NULL, 1, &vmem_handle);

	return 0;
}

//--- Mihaela ---//
GtkBuilder *builder; 

int counter = 0;
int blinkStart = 0;
int graphButtonClicked = 0; 

void * counter_thread(void * param) {
	
    while(1) {
		counter = counter + 1;
		sleep(1);
	}
	
	return NULL;
}//END counter_thread
		
void * ui_thread(void * param) {
    gtk_init(NULL, NULL);
    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, "ui.glade", NULL);
    gtk_builder_connect_signals(builder, NULL);
    gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(builder, "window_main")));                
    gtk_main();
    
	return NULL;
}//END ui_thread

void on_counterButton_clicked() {
	printf("\n");
	char buf[100];
	sprintf(buf, "Counter = %d", counter);
	gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "counterLabel"))), buf);
	printf("\nButton clicked\n");
	printf("Current counter nr: %d", counter);
}//END counterButton_clicked

//------------ PIN SETTINGS ----------//
int pinNum = 21;									//Choose pin Number to work with.
int blinkCounter = 0;
void * blink_thread(void * param){
	while(1){
		if(blinkStart == 1){
		blinkCounter = blinkCounter + 1;
		gpioWrite(pinNum,1);
		showPinLevel();
		sleep(1);
		gpioWrite(pinNum, 0);
		showPinLevel();
		}
		sleep(1);
	}
}//END blink_thread

void showPinMode(){
	printf("\nPin Mode: ");
	if(gpioGetMode(pinNum) == PI_OUTPUT){			//if pin is output, then
		printf("OUTPUT");
		char buf[100];
		sprintf(buf, "OUTPUT");						//Set text to "OUTPUT".
		gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "pinMode"))), buf);
	}else if(gpioGetMode(pinNum) == PI_INPUT){		//if pin is input, then
		printf("INPUT");
		char buf[100];
		sprintf(buf, "INPUT");						//Set text to "INPUT".
		gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "pinMode"))), buf);
	}else{											//if the chosen pin is not set as in/out-put.
		printf("not set.");						
		char buf[100];
		sprintf(buf, "None");						//Set text to "None".
		gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "pinMode"))), buf);
	}
	printf("\n");
}//END checkPinMode

void showPinLevel(){
	printf("\nPin Level:");
	printf("%d", gpioRead(pinNum));			//print pin bit status just set to high
	char buf[100];
	sprintf(buf, "\nLevel: %d", gpioRead(pinNum));
	gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "pinLevel"))), buf);
	printf("\n");
}

/*--- Pin Controls ---*/
void on_gpioButton_clicked(){
	/*---Show Pin Number ---*/
	printf("\nPin: %d", pinNum);
	char buf[100];
	sprintf(buf, "%d", pinNum);
	gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "gpioLabel"))), buf);
	printf("\n");
}//END pinNum_info

void on_pinOut_clicked(){
	gpioSetMode(pinNum, PI_OUTPUT);	
	showPinMode();
}//END pinOut_clicked

void on_pinIn_clicked(){
	gpioSetMode(pinNum, PI_INPUT);	
	showPinMode();
}//END pinIn_clicked

void on_pinHigh_clicked(){
	if(gpioGetMode(pinNum) == PI_OUTPUT){				//if pin is output, then
		gpioSetPullUpDown(pinNum, PI_PUD_UP);			//Set pull-up.
		gpioWrite(pinNum,1);							//set pin high, and
		showPinLevel();
	}else{
		printf("Error: ");
		showPinMode();
	}
}//END pinHigh_clicked

void on_pinLow_clicked(){
	if(gpioGetMode(pinNum) == PI_OUTPUT){				//if pin is output, then
		gpioWrite(pinNum,0);							//set pin high, and
		showPinLevel();
	}else{
		printf("Error: ");
		showPinMode();
	}
}//END pinLow_clicked

void on_pinBlink_clicked(){
	if(gpioGetMode(pinNum) == PI_OUTPUT){				//if pin is output, then
		gpioSetPullUpDown(pinNum, PI_PUD_UP);			//Set pull-up.
		blinkStart = 1;
	}else{
		printf("Error: ");		
		showPinMode();
	}
}//END pinBlink_clicked

void on_pinStopBlink_clicked(){
	blinkStart = 0;
	gpioWrite(pinNum, 0);
	showPinLevel();
	char buf[100];
	sprintf(buf, "\nSeconds \nblinked: %d", blinkCounter*2);
	gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "blinkCounter"))), buf);
	printf("\n");
}//END pinStopBlink_clicked
//___________________________END PIN SETTINGS________________//
//------------ ARRAY / SIGNAL ----------//
void * arrayProcess_thread(void * param){
		printf("arrayThread running\n");
		while(1){
			if(graphButtonClicked == 1) {
				/*Create array/signal*/
				int graphArray[10] = {1,2,3,4,5,6,7,8,9,10};
				for(int i=0; i<10; i++) {
					if (graphButtonClicked == 0)
						break;
					/*Write to processbar*/
					gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(GTK_WIDGET(gtk_builder_get_object(builder, "progressbar1"))), 0.1*i+0.1);
					gtk_progress_bar_set_text(GTK_PROGRESS_BAR(GTK_WIDGET(gtk_builder_get_object(builder, "progressbar1"))), "10V");
					/*Write to console*/
					printf("graphArray[%d] = %d\n",i,graphArray[i]);
					/*write to label*/
					char buf[100];
					sprintf(buf, "Graph = %d", graphArray[i]);
					gtk_label_set_text(GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(builder, "graphLabel"))), buf);
					/*delay 1 sec and print new line*/
					sleep(1);
					printf("\n");
				}
			}
		sleep(1);	
		}
		printf("\n");
		sleep(1);
	return NULL;
}//END arrayProcess_thread
	
void on_graphbutton_clicked(){ 
	graphButtonClicked = 1;
	printf("\nClicked\n");
}//END graphbutton_clicked

void on_switchArray_state_set(void) {
	if (gtk_switch_get_active(GTK_SWITCH(GTK_WIDGET(gtk_builder_get_object(builder, "switchArray"))))) {	
		printf("On\n");
		graphButtonClicked = 1;
	} else {
		printf("Off\n");
		graphButtonClicked = 0;
	}				
}

//--- Testing drawing SOMETHING in a drawingArea  :) :) ---// 
void graphArea_draw_cb(){
cairo_surface_t *surface;
cairo_t *cr;

surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 120, 120);
cr = cairo_create (surface);
	cairo_set_line_width (cr, 0.1);
cairo_set_source_rgb (cr, 0, 0, 0);
cairo_rectangle (cr, 0.25, 0.25, 0.5, 0.5);
cairo_stroke (cr);
    
	}
	//____ END TESTING DRAWING AREA ___//
//___________________________ END Mihaela _____________________________//

int main(int argc, char **argv)
{	
	//---------- CSS -------------
    GtkCssProvider *provider;
    GdkDisplay *display;
    GdkScreen *screen;
    
    provider = gtk_css_provider_new ();
    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    gtk_style_context_add_provider_for_screen (screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    const gchar *myCssFile = "style.css";
    GError *error = 0;
    gtk_css_provider_load_from_file(provider, g_file_new_for_path(myCssFile), &error);
    g_object_unref (provider);
    //---------------------------
    
	if (gpioInitialise() < 0)
   {
      fprintf(stderr, "pigpio initialisation failed\n");
      return 1;
   }
		
	static struct slash *slash;
	int remain, index, i, c, p = 0;
	char *ex;

	uint8_t addr = SATCTL_DEFAULT_ADDRESS;
	char *ifc = SATCTL_DEFAULT_INTERFACE;

	while ((c = getopt(argc, argv, "+hr:i:n:")) != -1) {
		switch (c) {
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'i':
			ifc = optarg;
			break;
		case 'n':
			addr = atoi(optarg);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	remain = argc - optind;
	index = optind;

	if (configure_csp(addr, ifc) < 0) {
		fprintf(stderr, "Failed to init CSP\n");
		exit(EXIT_FAILURE);
	}

	slash = slash_create(SATCTL_LINE_SIZE, SATCTL_HISTORY_SIZE);
	if (!slash) {
		fprintf(stderr, "Failed to init slash\n");
		exit(EXIT_FAILURE);
	}
	
	/* Setup UI */
	pthread_t ui_handle;
	pthread_create(&ui_handle, NULL, ui_thread, NULL);
	
	/* Setup Counter */
	pthread_t counter_handle;
	pthread_create(&counter_handle, NULL, counter_thread, NULL);
	
	/*Setup blink_thread */
	pthread_t blink_handle;
	pthread_create(&blink_handle, NULL, blink_thread, NULL);

	/* Setup arrayProcess_thread */
	pthread_t arrayProcess_handle;
	pthread_create(&arrayProcess_handle, NULL, arrayProcess_thread, NULL);
	
	/* Interactive or one-shot mode */
	if (remain > 0) {
		ex = malloc(SATCTL_LINE_SIZE);
		if (!ex) {
			fprintf(stderr, "Failed to allocate command memory");
			exit(EXIT_FAILURE);
		}

		/* Build command string */
		for (i = 0; i < remain; i++) {
			if (i > 0)
				p = ex - strncat(ex, " ", SATCTL_LINE_SIZE - p);
			p = ex - strncat(ex, argv[index + i], SATCTL_LINE_SIZE - p);
		}
		slash_execute(slash, ex);
		free(ex);
	} else {
		printf("\n\n");
		printf(" *******************************\n");
		printf(" **   SatCtl - Space Command  **\n");
		printf(" *******************************\n\n");

		printf(" Copyright (c) 2018 Space Inventor ApS <info@spaceinventor.com>\n");
		printf(" Copyright (c) 2014 Satlab ApS <satlab@satlab.com>\n\n");

		slash_loop(slash, SATCTL_PROMPT_GOOD, SATCTL_PROMPT_BAD);
	}

	slash_destroy(slash);

	return 0;
}
