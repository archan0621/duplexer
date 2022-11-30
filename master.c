#include "master.h"
#include "syshead.h"
#include "logger.h"
#include "ping.h"
#include "httpclient.h"
#include "basic.h"
#include "vip.h"

#define DUPLEXER_ALIVE "/duplexer/alive"
#define DUPLEXER_MYPLANE "/duplexer/myplane"
#define DUPLEXER_YOURPLANE "/duplexer/yourplane"

/* HA Connected */
//  GW 연결           DUP 연결         HA 연결
//   실패               실패            성공      VIP다운, Your plane
//   실패               성공            성공      VIP다운, Your plane   
//   성공               실패            성공      VIP업, My Plane, Dup점검 로그
//   성공               성공            성공      VIP업, My Plane

/* HA Disconnected */
//  GW 연결           DUP 연결         HA 연결
//   실패               실패            싪패      VIP다운
//   실패               성공            싪패      VIP다운
//   성공               실패            싪패      VIP업, Dup점검 로그
//   성공               성공            싪패      VIP업

void mode_master(context* c){

    int vip_status[2] = {0,}; // 1: up, 0: down
    int alive_count = 0, mode_flag = 0;

    logger(LOG_DEFAULT, "Entering Master Mode");

    /* initializing VIP */
    for (int i = 0; i < c->o.layer_count; i++){
        down_vip(c->o.l[i].interface);
        logger(LOG_DEFAULT, "INITIALIZING : Interface %s down", c->o.l[i].interface);
    }


    while(1){
        /* Checking HA */
        /* Using HA */
        if(c->o.direct == 1){
            /* 0: success 1: fail */
            if(ping_main(c->o.direct_ip, 3)!=0){
                c->s[0].ha_status = 1;
                c->s[1].ha_status = 1;
            }else {
                c->s[0].ha_status = 0;
                c->s[1].ha_status = 0;
                if(alive_count < 3){
                    logger(LOG_DEBUGGING,"[ MASTER ]: is Alive?");
                    if(send_http(c->o.direct_port, c->o.direct_ip, DUPLEXER_ALIVE)){
                        logger(LOG_DEBUGGING, "Cannot Connect Slave, waiting 3s.");
                        sleep(3);
                        alive_count++;
                        continue;
                    }else{
                        if(mode_flag == 0){
                            logger(LOG_DEFAULT, "Slave Connected, Change Direct on.");
                        }
                        mode_flag = 1;
                        alive_count = 0;
                    }
                }else if(alive_count == 3){
                    if(mode_flag == 1){
                        logger(LOG_DEFAULT, "Slave Disconnected, Change Direct off.");
                    }
                    mode_flag = 0;
                    alive_count++;
                    c->s[0].ha_status = 1;
                    c->s[1].ha_status = 1;
                }else{
                    c->s[0].ha_status = 1;
                    c->s[1].ha_status = 1;
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

        for (int i = 0; i < c->o.layer_count; i++){
            logger(LOG_DEBUGGING, "%d, %d", c->s[i].gw_status, c->s[i].dup_status);
            /* HA  Disonnected */
            if(c->s[i].ha_status){
                logger(LOG_DEBUGGING, "HA DISCONNECTED");
                /* GW, DUP Failed */
                if(c->s[i].gw_status && c->s[i].dup_status ){
                    logger(LOG_DEBUGGING, "GW, DUP FAILED");
                    /* VIP down */
                    if(vip_status[i]){
                        if(down_vip(c->o.l[0].interface)){
                            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[0].interface);
                        }else{
                            vip_status[0] = 0;
                        }

                        if(down_vip(c->o.l[1].interface)){
                            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[1].interface);
                        }else{
                            vip_status[1] = 0;
                        }
                    }
                }
                /* GW, DUP Success */
                else if (c->s[0].gw_status == 0 && c->s[1].gw_status == 0) {
                    /* VIP up */
                    logger(LOG_DEBUGGING, "GW, DUP Success");
                    if(vip_status[i] == 0){
                        if(install_vip(c->o.l[i].interface, c->o.l[i].vip)){
                            logger(LOG_DEFAULT,"Failed to install vip %s", c->o.l[i].interface);
                            break;
                        }

                        usleep(500);
                        if(install_netmask(c->o.l[i].interface, c->o.l[i].netmask)){
                            logger(LOG_DEFAULT,"Failed to install netmask %s", c->o.l[i].netmask);
                        }else{
                            logger(LOG_DEBUGGING,"Successfully installed vip");
                            vip_status[i] = 1;
                        }
                    }
                }
                else {
                    /* GW Fail, Dup Success */
                    if (c->s[i].gw_status) {
                        /* VIP down */
                        logger(LOG_DEBUGGING, "GW Failed, DUP Success");
                        if(vip_status[i]){
                        if(down_vip(c->o.l[0].interface)){
                            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[0].interface);
                        }else{
                            vip_status[0] = 0;
                        }

                        if(down_vip(c->o.l[1].interface)){
                            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[1].interface);
                        }else{
                            vip_status[1] = 0;
                        }
                        }
                    }
                    /* GW Success, Dup Fail */
                    else{
                        // logger(LOG_DEBUG, "GW Success, DUP FAILED");
                        // /* VIP up, check network log*/
                        // if(vip_status[i] == 0){
                        //     if(install_vip(c->o.l[i].interface, c->o.l[i].vip)){
                        //         logger(LOG_INFO,"Failed to install vip %s", c->o.l[i].interface);
                        //     }
                        //     vip_status[i] = 1;
                        //     usleep(500);
                        //     if(install_netmask(c->o.l[i].interface, c->o.l[i].netmask)){
                        //         logger(LOG_INFO,"Failed to install netmask %s", c->o.l[i].netmask);
                        //     }else{
                        //         vip_status[i] = 1;
                        //         logger(LOG_INFO, "Slave is Dead, Check network");
                        //     }
                        // }
                    }
                }
            }
            /* HA  Connected */
            else{
                logger(LOG_DEBUGGING, "HA CONNECTED");
                 /* GW, DUP Failed */
                if(c->s[i].gw_status && c->s[i].dup_status){
                    /* VIP down */
                    logger(LOG_DEBUGGING, "GW, DUP FAILED");
                    if(vip_status[i]){
                        if(down_vip(c->o.l[0].interface)){
                            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[0].interface);
                        }else{
                            vip_status[0] = 0;
                        }

                        if(down_vip(c->o.l[1].interface)){
                            logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[1].interface);
                        }else{
                            vip_status[1] = 0;
                        }
                    }
                    if(send_http(c->o.direct_port, c->o.direct_ip, DUPLEXER_YOURPLANE)){
                        logger(LOG_DEFAULT,"Failed to Send HA status");
                    }
                }
                /* GW, DUP Success */
                else if (c->s[0].gw_status == 0 && c->s[1].gw_status == 0) {
                    /* VIP up */
                    logger(LOG_DEBUGGING, "GW, DUP Success");
                    if(vip_status[i] == 0){
                        if(install_vip(c->o.l[i].interface, c->o.l[i].vip)){
                            logger(LOG_DEFAULT,"Failed to install vip %s", c->o.l[i].interface);
                            break;
                        }
                        usleep(500);   
                        if(install_netmask(c->o.l[i].interface, c->o.l[i].netmask)){
                            logger(LOG_DEFAULT,"Failed to install netmask %s", c->o.l[i].netmask);
                        }else{
                            logger(LOG_DEBUGGING,"Successfully installed vip");
                            vip_status[i] = 1;
                        } 
                    }
                    if(send_http(c->o.direct_port, c->o.direct_ip, DUPLEXER_MYPLANE)){
                        logger(LOG_DEFAULT,"Failed to Send HA status");
                    }
                } else {
                    /* GW Fail, Dup Success */
                    if (c->s[i].gw_status) {
                        /* VIP down */
                        logger(LOG_DEBUGGING, "GW Failed, DUP Success");
                        if(vip_status[i]){
                            if(down_vip(c->o.l[0].interface)){
                                logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[0].interface);
                            }else{
                                vip_status[0] = 0;
                            }

                            if(down_vip(c->o.l[1].interface)){
                                logger(LOG_DEFAULT,"Failed to down interface %s", c->o.l[1].interface);
                            }else{
                                vip_status[1] = 0;
                            }
                        }
                        if(send_http(c->o.direct_port, c->o.direct_ip, DUPLEXER_YOURPLANE)){
                            logger(LOG_DEFAULT,"Failed to Send HA status");
                        }
                    }
                    /* GW Success, Dup Fail */
                    else{
                        // /* VIP up, check network log*/
                        // logger(LOG_DEBUG, "GW Success, DUP FAILED");
                        // if(vip_status[i] == 0){
                        //     if(install_vip(c->o.l[i].interface, c->o.l[i].vip)){
                        //         logger(LOG_INFO,"Failed to install vip %s", c->o.l[i].interface);
                        //     }
                        //     vip_status[i] = 1;
                        //     usleep(500);
                        //     if(install_netmask(c->o.l[i].interface, c->o.l[i].netmask)){
                        //         logger(LOG_INFO,"Failed to install netmask %s", c->o.l[i].netmask);
                        //     }else{
                        //         vip_status[i] = 1;
                        //         logger(LOG_INFO, "Slave is Dead, Check network");
                        //     }
                        // }
                        // if(send_http(c->o.direct_port, c->o.direct_ip, DUPLEXER_MYPLANE)){
                        //     logger(LOG_INFO,"Failed to Send HA status");
                        // }  
                    }
                }
            }
        }
        logger(LOG_STATUS,"[ ======    STATUS   ====== ]");
        logger(LOG_STATUS,"1. Mode                  : Master");
        
        logger(LOG_STATUS,"2. Direct Mode           : %s", mode_flag ? "ON" : "OFF");
        if(mode_flag == 0){
            if(alive_count < 3){
                logger(LOG_STATUS,"2-1. Is Slave Connected? : NO");
            }else{
                logger(LOG_STATUS,"2-1. Is Slave Connected? : YES");
            }
        }
        for (int i = 0; i < c->o.layer_count; i++){
            logger(LOG_STATUS,"[ ======   layer-%d   ====== ]", i + 1);
            logger(LOG_STATUS,"3. Gateway    : %s", c->s[i].gw_status ? "DOWN" : "UP");
            logger(LOG_STATUS,"4. Slave      : %s", c->s[i].dup_status ? "DOWN" : "UP");
            logger(LOG_STATUS,"5. VIP        : %s", vip_status[0] ? "UP" : "DOWN");
        }
        
        alive_count = 0;
        sleep(2);
    }
}