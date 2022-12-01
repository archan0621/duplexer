#include "master.h"
#include "syshead.h"
#include "logger.h"
#include "httpserver.h"
#include "ping.h"
#include "vip.h"

int port;
extern int check_alive;

void* t_function(void* data) {
    http_server(port, (int*)data);
    return NULL;
}

/* HA Connected */
// 마스터 설정을 따라감

/* HA Disconnected */
//  GW 연결           DUP 연결         HA 연결
//   실패               실패            실패      VIP다운
//   실패               성공            실패      VIP다운
//   성공               실패            실패      VIP업, Dup점검 로그
//   성공               성공            실패      VIP다운

void mode_slave(context* c){
    logger(LOG_DEFAULT, "Entering Slave Mode");

    pthread_t pthread;
    /* 1: Master's Plane, 2: Slave's Plane */
    int pilot = 0;
                                                         /*  0: down 1: up   */
    int thr_id, result, mode_flag = 0, alive_count = 0, install_vip_now = 0;
    port = c->o.direct_port;

     for (int i = 0; i < c->o.layer_count; i++){
       if(down_vip(c->o.l[i].interface)){
            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[i].interface);
        }else{
            c->s[i].vip_status = 0;
            logger(LOG_DEFAULT, "INITIALIZING : Interface %s down", c->o.l[i].interface);
        }
    }
    
    if(c->o.direct == 1){
        thr_id = pthread_create(&pthread, NULL, t_function, (void*)&pilot);
        if(thr_id < 0) {
          logger(LOG_DEFAULT,"pthread0 create error");
          exit(EXIT_FAILURE);
        }
    }

    while(1){
        /* Checking HA */
        /* Using HA */
        if(c->o.direct == 1){
            if(ping_main(c->o.direct_ip, 3)!=0){
                c->s[0].ha_status = 1;
                c->s[1].ha_status = 1;
            }else {
                c->s[0].ha_status = 0;
                c->s[1].ha_status = 0;
                for( int i = 0; i < 3 ; i ++){
                    if(check_alive != 0){
                        break;
                    } else {
                        alive_count ++;
                        sleep(2);
                    }
                }
                if (check_alive == 0){
                    if(mode_flag == 1){
                        logger(LOG_DEFAULT, "Master Disconnected, Change Direct off.");
                    }
                    mode_flag = 0;
                    c->s[0].ha_status = 1;
                    c->s[1].ha_status = 1;
                }

                if (check_alive == 1){
                    if(mode_flag == 0){
                        logger(LOG_DEFAULT, "Master Connected, Change Direct on.");
                    }
                    mode_flag = 1;
                    c->s[0].ha_status = 0;
                    c->s[1].ha_status = 0;
                }
            }
        }
        /* Not Using HA */
        else {
            c->s[0].ha_status = 1;
            c->s[1].ha_status = 1;
        }
        
        /* Check GW, Opponent(dup) */
        for (int i = 0; i < c->o.layer_count; i++){
            if(ping_main(c->o.l[i].gateway, c->o.l[i].count)!=0){
                c->s[i].gw_status = 1;
            }else{
                c->s[i].gw_status = 0;
            }
            
            if(ping_main(c->o.l[i].dup, c->o.l[i].count)!=0){
                c->s[i].dup_status = 1;
            }else{
                c->s[i].dup_status = 0;
            }
        }

        install_vip_now = 0;
        for (int i = 0; i < c->o.layer_count; i++){
            /* HA Disonnected */
            if(c->s[i].ha_status){
                logger(LOG_DEBUGGING, "HA DISCONNECTED");
                /* GW, DUP Failed */
                if(c->s[i].gw_status && c->s[i].dup_status ){
                    logger(LOG_DEBUGGING, "GW, DUP FAILED");
                    /* VIP down */
                }
                /* GW, DUP Success */
                else if (c->s[i].gw_status == 0 && c->s[i].dup_status == 0 ) {
                    logger(LOG_DEBUGGING, "GW, DUP Success");
                    /* VIP down */
                } else {
                    /* GW Fail, Dup Success */
                     if (c->s[i].gw_status){
                        logger(LOG_DEBUGGING, "GW Failed, DUP Success");
                        /* VIP down */
                     }
                     /* GW Success, Dup Fail */
                     else{
                        logger(LOG_DEBUGGING, "GW Success, DUP FAILED");
                        /* VIP up */
                        install_vip_now = 1;
                     }
                }
            }
            /* HA Connected */
            else{
                logger(LOG_DEBUGGING, "HA CONNECTED");
                /* Master's Plane */
                if(pilot == 1){
                    logger(LOG_DEBUGGING, "Master Priority, VIP down" );
                } 
                /* Slave's Plane */
                else if (pilot == 2){
                    logger(LOG_DEBUGGING, "Slave Priority, Need Installing VIP" );
                    install_vip_now = 1;
                }
                /* HA Disconnected */
                else{
                    logger(LOG_DEFAULT, "Master is not Connected" );
                }
            }
        }

        /* Managing VIP */
        for (int i = 0; i < c->o.layer_count; i++){
            if(install_vip_now){
                if(c->s[i].vip_status == 0){
                    if(install_vip(c->o.l[i].interface, c->o.l[i].vip)){
                        logger(LOG_DEFAULT,"Failed to install vip %s", c->o.l[i].interface);
                    }
                    usleep(200);
                    if(install_netmask(c->o.l[i].interface, c->o.l[i].netmask)){
                        logger(LOG_DEFAULT,"Failed to install netmask %s", c->o.l[i].netmask);
                    }
                    usleep(200);
                    if(check_vip(c->o.l[i].interface, c->o.l[i].vip) == 0){
                        c->s[i].vip_status = 1;
                    }
                }
            }else{
                if(c->s[i].vip_status){
                    if(down_vip(c->o.l[i].interface)){
                        logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[i].interface);
                    }else{
                        c->s[i].vip_status = 0;
                    }
                    usleep(200);
                }
            }
        }

        logger(LOG_STATUS,"[ ======    STATUS   ====== ]");
        logger(LOG_STATUS,"1. Mode                   : Slave");
        
        logger(LOG_STATUS,"2. Direct Mode            : %s", mode_flag ? "ON" : "OFF");
        if(mode_flag == 0){
            if(alive_count < 3){
                logger(LOG_STATUS,"2-1. Is Master Connected? : NO");
            }else{
                logger(LOG_STATUS,"2-1. Is Master Connected? : YES");
            }
        }
        for (int i = 0; i < c->o.layer_count; i++){
            logger(LOG_STATUS,"[ ======   layer-%d   ====== ]", i + 1);
            logger(LOG_STATUS,"3. Gateway    : %s", c->s[i].gw_status ? "DOWN" : "UP");
            logger(LOG_STATUS,"4. Master     : %s", c->s[i].dup_status ? "DOWN" : "UP");
            logger(LOG_STATUS,"5. VIP        : %s", c->s[i].vip_status ? "UP" : "DOWN");
        }
        alive_count = 0;
        check_alive = 0;
        sleep(2);
    }

    pthread_join(pthread, NULL);
}
