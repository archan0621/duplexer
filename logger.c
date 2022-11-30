#include "logger.h"
#include "syshead.h"

int log_level;
int use_syslog; // use : 1
int use_file_log;
char log_file[256];

void open_syslog(){
	openlog("Duplexer", LOG_PID, LOG_DAEMON);
}

void close_syslog(){
	closelog();
}

void logger_(int level, const char* funcname, void* format, ...)
{
	char buf[1024] = { 0, };

	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	if ((level & log_level) == 0) {
		return ;
	}

	if (level == 1) {
		sprintf(buf, "%d-%d-%d %d:%d:%d [INFO] [FUNC: %s] ", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, funcname);
	}else if (level == 2) {
		sprintf(buf, "%d-%d-%d %d:%d:%d [DEBUG] [FUNC: %s] ", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,  funcname);
	}else if (level == 4) {
		sprintf(buf, "%d-%d-%d %d:%d:%d [STATUS] [FUNC: %s] ", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,  funcname);
	}

	// else if (level == 4) {
	// 	if (strstr(format, "%s") != NULL) {
	// 		sprintf(buf, "%d-%d-%d %d:%d:%d [HEX] [FUNC: %s] ", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, funcname);
	// 	}
	// }

	// if (level == 4) {
	// 	if (strstr(format, "%s") == NULL) {
	// 		dump_hex(format, strlen(format));
	// 		return ;
	// 	}
	// }

	va_list va;
	va_start(va, format);
	vsprintf(buf + strlen(buf), format, va);
	va_end(va);


	if( use_syslog  == 1 ){
		syslog(LOG_DEFAULT,"%s", buf);
	}

	if ( use_file_log == 1){
		if(strlen(log_file)<1){
			use_file_log = 0;
			return;
		}

		FILE* fp = fopen(log_file, "a+");

    	if(fp == NULL){
			use_file_log = 0;
        	logger(LOG_DEFAULT,"Error Opening log file [%s]", log_file);
        	return ;
    	}
		fprintf(fp, "%s\n",buf );
		fclose(fp);
	}else{
		puts(buf);
	}

}


void dump_hex(const void* data, int size) {
	char ascii[17];
	int i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		}
		else {
			ascii[i % 16] = '.';
		}
		if ((i + 1) % 8 == 0 || i + 1 == size) {
			printf(" ");
			if ((i + 1) % 16 == 0) {
				printf("|  %s \n", ascii);
			}
			else if (i + 1 == size) {
				ascii[(i + 1) % 16] = '\0';
				if ((i + 1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i + 1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}
