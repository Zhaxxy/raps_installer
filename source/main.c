#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <sys/stat.h>
#include <dirent.h>

#include <io/pad.h>

#include <sys/thread.h>

#include <ppu-types.h>

#include <sysmodule/sysmodule.h>

#include <sysutil/msg.h>
#include <sysutil/sysutil.h>

#include "rsxutil.h"

#include "rap2rif/rap2rif.h"
#include "rap2rif/tools.h"

#define VERSION_STRING "3.1"
#define VERSION_PRETTY "\nVersion "VERSION_STRING

#define QUIT_THREAD(error) thread_results->finished_yet = true; sysThreadExit(error); return
#define THREAD_ERROR_NO_KEY_FOUND 1
#define THREAD_ERROR_NO_ACT_DAT_FOUND 2
#define THREAD_ERROR_BAD_RAP_NAME 3
#define THREAD_ERROR_SOMETHING_WENT_WRONG_RAP2RIF 4

struct ThreadResults {
	bool finished_yet;
	int new_raps_installed;
	int raps_found;
	char bad_rap_name[2048];
};

#define FOLDER_TO_CHECK_FOR_RAPS_SIZE 10
const char FOLDER_TO_CHECK_FOR_RAPS[FOLDER_TO_CHECK_FOR_RAPS_SIZE][32] = {
	"/dev_hdd0/packages/temp_raps/",
	"/dev_hdd0/exdata/",
	"/dev_usb000/exdata/",
	"/dev_usb001/exdata/",
	"/dev_usb002/exdata/",
	"/dev_usb003/exdata/",
	"/dev_usb004/exdata/",
	"/dev_usb005/exdata/",
	"/dev_usb006/exdata/",
	"/dev_usb007/exdata/",
};

static vs32 dialog_action = 0;


static void dialog_handler(msgButton button,void *usrData)
{
	switch(button) {
		case MSG_DIALOG_BTN_OK:
			dialog_action = 1;
			break;
		case MSG_DIALOG_BTN_NO:
		case MSG_DIALOG_BTN_ESCAPE:
			dialog_action = 2;
			break;
		case MSG_DIALOG_BTN_NONE:
			dialog_action = -1;
			break;
		default:
			break;
	}
}

static void program_exit_callback()
{
	gcmSetWaitFlip(context);
	rsxFinish(context,1);
}

static void sysutil_exit_callback(u64 status,u64 param,void *usrdata)
{
	switch(status) {
		case SYSUTIL_EXIT_GAME:
			break;
		case SYSUTIL_DRAW_BEGIN:
		case SYSUTIL_DRAW_END:
			break;
		default:
			break;
	}
}


static void do_flip()
{
	sysUtilCheckCallback();
	flip();
}

void install_rap_thread(void *arg)
{
	u64 idps[2];
	struct ThreadResults *thread_results = arg;
	thread_results->raps_found = 0;
	thread_results->new_raps_installed = 0;
	
	int index_for_folder_to_check;
	struct stat stat_buffer;
	DIR *dir;
	// DIR *sub_dir; // TODO i might make it look through user home folder for raps, but not now
	struct dirent *entry;
	// struct dirent *sub_entry; // ^
	
	char exdata_folder[1056];
	char rif_output[1056 + sizeof("UP9000-NPUA80662_00-GLITTLEBIG000001.rif")];
	
	struct keylist *klist = NULL;
	struct actdat *actdat = NULL;
	
	int rap2rif_res;
	
	char content_id[sizeof("UP9000-NPUA80662_00-GLITTLEBIG000001")];
	
	lv2syscall2(867, 0x19003, (u8*)(u64)idps);

	klist = keys_get(KEY_NPDRM,idps);
	if (klist == NULL) {
		QUIT_THREAD(THREAD_ERROR_NO_KEY_FOUND);
	}
	
	actdat = actdat_get(exdata_folder);
	if (actdat == NULL) {
		QUIT_THREAD(THREAD_ERROR_NO_ACT_DAT_FOUND);
	}
	
	/* now its time to cook, lets see where we can find rap files
	   not gonna recusrivly check though */
	for (index_for_folder_to_check = 0; index_for_folder_to_check < FOLDER_TO_CHECK_FOR_RAPS_SIZE; index_for_folder_to_check++) {
		dir = opendir(FOLDER_TO_CHECK_FOR_RAPS[index_for_folder_to_check]);
		entry = NULL;
		if (dir) {
			while ((entry = readdir(dir)) != NULL) {
				if (entry->d_type == DT_DIR) {
					continue;
				}
				if (!(strlen(entry->d_name) > strlen(".rap") && !strcmp(entry->d_name + strlen(entry->d_name) - strlen(".rap"), ".rap"))) {
					continue;
				}
				sprintf(thread_results->bad_rap_name,"%s%s",FOLDER_TO_CHECK_FOR_RAPS[index_for_folder_to_check],entry->d_name);
				if (strlen(entry->d_name) != (sizeof(content_id)-1) + strlen(".rap")) {
					closedir(dir);
					QUIT_THREAD(THREAD_ERROR_BAD_RAP_NAME);
				}
				thread_results->raps_found++;
				strncpy(content_id,entry->d_name,(sizeof(content_id)-1));
				sprintf(rif_output,"%s%s.rif",exdata_folder,content_id);
				if (stat(rif_output, &stat_buffer) != 0) {
					thread_results->new_raps_installed++;
				}
				rap2rif_res = rap2rif(actdat,klist,content_id,thread_results->bad_rap_name,rif_output);
				if (rap2rif_res != 0) {
					closedir(dir);
					QUIT_THREAD(THREAD_ERROR_SOMETHING_WENT_WRONG_RAP2RIF);
				}
				
			}
			closedir(dir);
		}
	}
	// sleep(5);
	QUIT_THREAD(0);
	
}

int main(int argc,char *argv[])
{
	char result_message[4096];
	sys_ppu_thread_t thread_id;
	u64 thread_retval;
	struct ThreadResults thread_results;
	
	thread_results.finished_yet = false;
	
	
    msgType dialogType;
 	void *host_addr = memalign(1024*1024,HOST_SIZE);

	init_screen(host_addr,HOST_SIZE);
	ioPadInit(7);

	atexit(program_exit_callback);
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0,sysutil_exit_callback,NULL);
	
	msgDialogClose(3000.0f);

	
	sysThreadCreate(&thread_id,install_rap_thread,(void *)&thread_results,1500,0x500000,THREAD_JOINABLE,"rap_installer_thread");
	
	dialogType = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_OK);
	msgDialogOpen2(dialogType,"Please wait for us to find and install the rap files"VERSION_PRETTY,dialog_handler,NULL,NULL);
	dialog_action = 0;
	
	while (!thread_results.finished_yet) {
		do_flip();
	}
	msgDialogAbort();
	
	sysThreadJoin(thread_id,&thread_retval);
	switch (thread_retval) {
		case 0:
			snprintf(result_message,sizeof(result_message),"Installed %d rap files! Installed %d new rap files!"VERSION_PRETTY,thread_results.raps_found,thread_results.new_raps_installed);
			dialogType = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_OK);
			msgDialogOpen2(dialogType,result_message,dialog_handler,NULL,NULL);
			break;
		case THREAD_ERROR_NO_KEY_FOUND:
			dialogType = (msgType)(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_OK);
			msgDialogOpen2(dialogType,"No key files found. Did you install the pkg file and run it normally?"VERSION_PRETTY,dialog_handler,NULL,NULL);
			break;
		case THREAD_ERROR_NO_ACT_DAT_FOUND:
			dialogType = (msgType)(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_OK);
			msgDialogOpen2(dialogType,"No act.dat file could be found. Please make sure you have activated at least one of your local users, you can offline activate using Apollo save tool."VERSION_PRETTY,dialog_handler,NULL,NULL);
			break;
		case THREAD_ERROR_BAD_RAP_NAME:
			snprintf(result_message,sizeof(result_message),"rap file:\n%s\nhas a bad rap filename."VERSION_PRETTY,thread_results.bad_rap_name);
			dialogType = (msgType)(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_OK);
			msgDialogOpen2(dialogType,result_message,dialog_handler,NULL,NULL);
			break;
		case THREAD_ERROR_SOMETHING_WENT_WRONG_RAP2RIF:
			snprintf(result_message,sizeof(result_message),"something went wrong converting rap file:\n%s\nto rif file"VERSION_PRETTY,thread_results.bad_rap_name);
			dialogType = (msgType)(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_OK);
			msgDialogOpen2(dialogType,result_message,dialog_handler,NULL,NULL);
			break;
		default:
			dialogType = (msgType)(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_OK);
			msgDialogOpen2(dialogType,"Unknown error has occured"VERSION_STRING,dialog_handler,NULL,NULL);
			break;
	}
	dialog_action = 0;
	while (!dialog_action) {
		do_flip();
	}
	msgDialogAbort();
	
    return 0;
}
