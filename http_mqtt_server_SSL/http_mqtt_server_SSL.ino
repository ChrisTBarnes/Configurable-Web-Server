/********************************************************************
 * web_server - example code providing a framework that supports
 * either an http web server or mqtt client that responds to 
 * server requests
 * 
 * created Jan-6,  2016
 * revised Apr-26, 2017  Added TLS support for MQTT connections
 * by Dave St. Aubin
 * 
 * Compiled using Arduino 1.8.2
 * 
 * This example code is in the public domain.
 ********************************************************************/

#include "sketch.h"
#include <ESP8266WiFi.h>          //http server library
#include <PubSubClient.h>         //MQTT server library
#include <WiFiClientSecure.h>

#include <ESP8266mDNS.h>          //OTA libraries
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <time.h>

#include <UtilityFunctions.h>
#include <stdint.h>
#include <EEPROM.h>
#include "PAGE_Style.css.h"
#include "PAGE_NetworkConfiguration.h"

// Include API-Headers
extern "C" {                      //SDK functions for Arduino IDE access 
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem_manager.h"
#include "mem.h"
#include "string.h"
#include "user_interface.h"
#include "cont.h"
#include "espconn.h"
#include "eagle_soc.h"
#include <pgmspace.h>
void * pvPortZalloc(int size,char *, int);
}

/********************************************************
 * Global Variables
 ********************************************************/
   
//----Initial EEPROM Values------//
char init_ssid[32] = "YOUR_WIFI_SSID";            
char init_pass[32] = "YOUR_WIFI_PW";     
const char* init_ip0 = "192";
const char* init_ip1 = "168";
const char* init_ip2 = "0";
const char* init_ip3 = "133";
const char* init_nm0 = "255";
const char* init_nm1 = "255";
const char* init_nm2 = "255";
const char* init_nm3 = "0";
const char* init_gw0 = "192";
const char* init_gw1 = "168";
const char* init_gw2 = "0";
const char* init_gw3 = "1";
const char* init_ap0 = "192";
const char* init_ap1 = "168";
const char* init_ap2 = "4";
const char* init_ap3 = "1";
const char* init_port = "9705";
const char* init_fota_port = "18266";
const char* init_fota_pass = "";
const char* init_mqtt_port = "8883";
const char* init_bk = "yourmqttbroker.com";
const char* init_ci = "mqtt_clientID";
const char* init_un = "mqtt_username";
const char* init_pw = "mqtt_password";
const char* init_rt = "MyMqttSvrRqst";
const char* init_tt = "MyMqttSvrRply";
const char* init_bd = "115200";
const char* init_sr = "false";
const char* init_mqpwen = "false";
const char* init_mqsslen = "false";
const char* init_mqclen = "false";
String mqtt_svr = "";
String mqtt_ci = "";
String mqtt_un = "";
String mqtt_pw = "";
String mqtt_rt = "";
String mqtt_tt = "";
bool arduino_server =false;
bool mqtt_pw_enable =false;
bool mqtt_ssl_enable =false;
bool mqtt_cl_enable =false;
int http_port;
int mqtt_port;
int serial_baud;

int timezone = 0;
int dst = 0;
bool gmt = false;


//SerialEvent
String SerialInString;
bool NewSerialLineRx=false;
int active_svr_rqst=SVR_NONE;
long start_wait=0;

long lastMsg = 0;
uint32_t state=0;
int initRx=0;
int stoprepub = 0;
int sz,blinkarg[4];
static int busy=0;
float Ain;
char Ain0[20],Ain1[20],Ain2[20],Ain3[20],Ain4[20],Ain5[20],Ain6[20],Ain7[20]; 
bool complete=false;
int lc=0;
int wifi=99;

//sdk web server
char *precvbuffer;
static uint32 dat_sumlength = 0;
bool httpsend_ready = true;

/********************************************************
 * Local Function Prototypes
 ********************************************************/
void SetEepromCfg(void *arg);
void SetSysCfgFromEeprom(void);
void InitEepromValues(void);
void printEEprom(void);
void SetEepromAscii(void *arg, int index, int eeprom_offset);
void GetEepromCfg(String *page);
void SetCfgPageWithEepromVal(String *page, String par, int offset, int fmt);
void GetEepromVal(String *val,int offset, int fmt);
void SetEepromVal(char *val,int offset, int fmt);

void SdkWebServer_Init(int port);
void SdkWebServer_listen(void *arg);
void SdkWebServer_recv(void *arg, char *pusrdata, unsigned short length);
void SdkWebServer_discon(void *arg);
void SdkWebServer_recon(void *arg, sint8 err);
void SdkWebServer_senddata(void *arg, bool responseOK, char *psend, char *contenttype);
bool SdkWebServer_savedata(char *precv, uint16 length);
void SdkWebServer_parse_url_params(char *precv, URL_Param *purl_param);
void SdkWebServer_sent_cb(void *arg);

void util_printStatus(char * status, int s);
void util_startWIFI(void);

void jsonEncode(int pos, String * s, String key, String val);
void jsonAdd(String *s, String key,String val);

void MqttServer_Init(void);
void MqttServer_reconnect(void);  
void MqttServer_callback(char* topic, byte* payload, unsigned int length);
void MqttServer_Processor(void);

void Server_ProcessRequest(int servertype, char *payload, char *reply);
void Server_ExecuteRequest(int servertype,RQST_Param rparam, String* reply, String ArduinoRequest);
void Server_SendReply(int servertype, int replytype, String payld);

void ReadSensors(int interval);
void ResetEspTimerCallback(void);

void BlinkCallback(int *pArg);

void wifi_event_cb(System_Event_t *evt);

String ArduinoSendReceive(String req);
void MonitorSerialLine();
void init_GmtTime();

/********************************************************
 * Instantiate class objects
 ********************************************************/

WiFiClientSecure sslClient;
//PubSubClient client(sslClient);   //Create Mqtt Client object
PubSubClient* client;   //Create Mqtt Client object

WiFiClient espClient;             //Create Wifi Client object
//PubSubClient client(espClient);   //Create Mqtt Client object

os_timer_t BlinkLedTimer;         //Timer to blink LED
os_timer_t ResetEspTimer;         //Timer to reset Esp8266
struct espconn *ptrespconn;

/********************************************************
 * Set MQTT Topic Using ESP8266 MAC
 * 
 * 
 * 
 ********************************************************/

 void AddMAC(char * prefix, char * topic) {
     uint8_t MAC_array[6];
     WiFi.macAddress(MAC_array);
     sprintf(topic,"%s", prefix);
     for (int i = 0; i < sizeof(MAC_array); ++i){
          sprintf(topic,"%s%02x",topic, MAC_array[i]);
     }
 }

/********************************************************
 *  URL Parameter Decoder
 ********************************************************/
 
int url_decode(char *encoded_str, char *decoded_str) {
   
    // While we're not at the end of the string (current character not NULL)
    while (*encoded_str) {
        // Check to see if the current character is a %
        if (*encoded_str == '%') {
    
            // Grab the next two characters and move encoded_str forwards
            encoded_str++;
            char high = *encoded_str;
            encoded_str++;
            char low = *encoded_str;
    
            // Convert ASCII 0-9A-F to a value 0-15
            if (high > 0x39) high -= 7;
            high &= 0x0f;
    
            // Same again for the low byte:
            if (low > 0x39) low -= 7;
            low &= 0x0f;
    
            // Combine the two into a single byte and store in decoded_str:
            *decoded_str = (high << 4) | low;
        } else {
            // All other characters copy verbatim
            *decoded_str = *encoded_str;
        }
    
        // Move both pointers to the next character:
        encoded_str++;
        decoded_str++;
    }
    // Terminate the new string with a NULL character to trim it off
    *decoded_str = 0;
}


/********************************************************
 * Update EEPROM configuration
 * Function: SetEepromCfg(URL_Param *pURL_Param)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *pURL_Param  pointer to parameters
 * return       no return value
 ********************************************************/
void SetEepromCfg(void *arg)
{
    int i,j,serserver=0,mqtt_pwen=0,mqtt_sslen=0,mqtt_clen=0;
    struct URL_Param *pURL_Param = (URL_Param *)arg;
    char szT[64];
    for(i=0;i<pURL_Param->nPar;i++)
    {
        if(os_strcmp(pURL_Param->pParam[i], "ssid")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_WIFISSID);
        }
        if(os_strcmp(pURL_Param->pParam[i], "password")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_WIFIPASS);
        }
        if(os_strcmp(pURL_Param->pParam[i], "fota_pw")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_FOTA_PW);
        }
        if(os_strcmp(pURL_Param->pParam[i], "fota_pt")==0) {
            EEPROM.write(EEPROM_FOTA_PT+1, (uint8_t) (atoi(pURL_Param->pParVal[i])&0xFF));
            EEPROM.write(EEPROM_FOTA_PT, (uint8_t) ((atoi(pURL_Param->pParVal[i])>>8)&0xFF));
        }

        if(os_strcmp(pURL_Param->pParam[i], "ip_0")==0) EEPROM.write(EEPROM_WIFI_IP0, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "ip_1")==0) EEPROM.write(EEPROM_WIFI_IP1, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "ip_2")==0) EEPROM.write(EEPROM_WIFI_IP2, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "ip_3")==0) EEPROM.write(EEPROM_WIFI_IP3, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "nm_0")==0) EEPROM.write(EEPROM_WIFI_NM0, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "nm_1")==0) EEPROM.write(EEPROM_WIFI_NM1, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "nm_2")==0) EEPROM.write(EEPROM_WIFI_NM2, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "nm_3")==0) EEPROM.write(EEPROM_WIFI_NM3, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "gw_0")==0) EEPROM.write(EEPROM_WIFI_GW0, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "gw_1")==0) EEPROM.write(EEPROM_WIFI_GW1, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "gw_2")==0) EEPROM.write(EEPROM_WIFI_GW2, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "gw_3")==0) EEPROM.write(EEPROM_WIFI_GW3, (uint8_t) atoi(pURL_Param->pParVal[i]));
        if(os_strcmp(pURL_Param->pParam[i], "ap_0")==0) EEPROM.write(EEPROM_WIFI_AP0, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "ap_1")==0) EEPROM.write(EEPROM_WIFI_AP1, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "ap_2")==0) EEPROM.write(EEPROM_WIFI_AP2, (uint8_t) atoi(pURL_Param->pParVal[i])); 
        if(os_strcmp(pURL_Param->pParam[i], "ap_3")==0) EEPROM.write(EEPROM_WIFI_AP3, (uint8_t) atoi(pURL_Param->pParVal[i]));
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_bk")==0) SetEepromAscii((void *)pURL_Param, i, EEPROM_MQTT_BK);
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_ci")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_MQTT_CI);
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_un")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_MQTT_UN);
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_pw")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_MQTT_PW);
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_rt")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_MQTT_RT);
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_tt")==0) {
            url_decode((char *)pURL_Param->pParVal[i], (char *)szT );
            os_strcpy(pURL_Param->pParVal[i],szT);
            SetEepromAscii((void *)pURL_Param, i, EEPROM_MQTT_TT);
        }
        if(os_strcmp(pURL_Param->pParam[i], "ser_baud")==0) {
            EEPROM.write(EEPROM_SER_BAUD+2, (int) (atoi(pURL_Param->pParVal[i])&0xFF));
            EEPROM.write(EEPROM_SER_BAUD+1, (int) ((atoi(pURL_Param->pParVal[i])>>8)&0xFF));
            EEPROM.write(EEPROM_SER_BAUD+0, (int) ((atoi(pURL_Param->pParVal[i])>>16)&0xFF));
        }
        if(os_strcmp(pURL_Param->pParam[i], "ser_serv")==0) {
            SetEepromVal((char *)"true",EEPROM_SER_SERV, EEPROM_CHR);
            serserver=1;
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_pw_en")==0) {
            SetEepromVal((char *)"true",EEPROM_MQTTPWEN, EEPROM_CHR);
            mqtt_pwen=1;
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_ssl_en")==0) {
            SetEepromVal((char *)"true",EEPROM_MQTTSSLEN, EEPROM_CHR);
            mqtt_sslen=1;
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_cl_en")==0) {
            SetEepromVal((char *)"true",EEPROM_MQTTCLEN, EEPROM_CHR);
            mqtt_clen=1;
        }
        if(os_strcmp(pURL_Param->pParam[i], "mqtt_pt")==0) {
            EEPROM.write(EEPROM_MQTT_PT+1, (uint8_t) (atoi(pURL_Param->pParVal[i])&0xFF));
            EEPROM.write(EEPROM_MQTT_PT, (uint8_t) ((atoi(pURL_Param->pParVal[i])>>8)&0xFF));
        }
        if(os_strcmp(pURL_Param->pParam[i], "svrport")==0) {
            EEPROM.write(EEPROM_SVR_PORT+1, (uint8_t) (atoi(pURL_Param->pParVal[i])&0xFF));
            EEPROM.write(EEPROM_SVR_PORT, (uint8_t) ((atoi(pURL_Param->pParVal[i])>>8)&0xFF));
        }
    }
    if(!serserver) { //Set Arduino Server via serial off if not in parameter list
        SetEepromVal((char *)"false",EEPROM_SER_SERV, EEPROM_CHR);  
    }
    if(!mqtt_pwen) { //Set MQTT Password Enable off if not in parameter list
        SetEepromVal((char *)"false",EEPROM_MQTTPWEN, EEPROM_CHR);  
    }
    if(!mqtt_sslen) { //Set MQTT SSL Enable off if not in parameter list
        SetEepromVal((char *)"false",EEPROM_MQTTSSLEN, EEPROM_CHR);  
    }
    if(!mqtt_clen) { //Set MQTT Client Enable off if not in parameter list
        SetEepromVal((char *)"false",EEPROM_MQTTCLEN, EEPROM_CHR);  
    }
    EEPROM.commit();
}

/********************************************************
 * Sets variables associated with EEPROM residing values
 * for use during execution so EEPROM does not need to be 
 * read excessively.
 * Function: SetSysCfgFromEeprom(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void SetSysCfgFromEeprom(void) {
    String Param="";
    // Initialize MQTT Parameters for this session
    GetEepromVal(&mqtt_svr, EEPROM_MQTT_BK, EEPROM_CHR); 
    GetEepromVal(&mqtt_rt, EEPROM_MQTT_RT, EEPROM_CHR); 
    GetEepromVal(&mqtt_tt, EEPROM_MQTT_TT, EEPROM_CHR); 
    GetEepromVal(&mqtt_ci, EEPROM_MQTT_CI, EEPROM_CHR);
    GetEepromVal(&mqtt_un, EEPROM_MQTT_UN, EEPROM_CHR);
    GetEepromVal(&mqtt_pw, EEPROM_MQTT_PW, EEPROM_CHR);
    // Initialize Network Parameters for this session 
    GetEepromVal(&Param, EEPROM_SVR_PORT, EEPROM_INT16);
    http_port = atoi(Param.c_str());
    GetEepromVal(&Param, EEPROM_MQTT_PT, EEPROM_INT16);
    mqtt_port = atoi(Param.c_str());
     // Get Serial baud for this session
    Param="";
    GetEepromVal(&Param, EEPROM_SER_BAUD, EEPROM_INT24);
    serial_baud = atoi(Param.c_str());       
    // Enable Servers for this session
    Param="";
    GetEepromVal(&Param, EEPROM_SER_SERV, EEPROM_CHR); 
    arduino_server = os_strcmp(Param.c_str(), "true")==0; 
    GetEepromVal(&Param, EEPROM_MQTTPWEN, EEPROM_CHR); 
    mqtt_pw_enable = os_strcmp(Param.c_str(), "true")==0; 
    GetEepromVal(&Param, EEPROM_MQTTSSLEN, EEPROM_CHR); 
    mqtt_ssl_enable = os_strcmp(Param.c_str(), "true")==0; 
    GetEepromVal(&Param, EEPROM_MQTTCLEN, EEPROM_CHR); 
    mqtt_cl_enable = os_strcmp(Param.c_str(), "true")==0; 
}

/********************************************************
 * Initialize EEPROM with default values
 * Function: InitEepromValues(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void InitEepromValues(void) {
    char topic[20];
    SetEepromVal((char *)init_ssid,EEPROM_WIFISSID, EEPROM_CHR);
    SetEepromVal((char *)init_pass,EEPROM_WIFIPASS, EEPROM_CHR);
    SetEepromVal((char *)init_ip0,EEPROM_WIFI_IP0, EEPROM_INT);
    SetEepromVal((char *)init_ip1,EEPROM_WIFI_IP1, EEPROM_INT);
    SetEepromVal((char *)init_ip2,EEPROM_WIFI_IP2, EEPROM_INT);
    SetEepromVal((char *)init_ip3,EEPROM_WIFI_IP3, EEPROM_INT);
    SetEepromVal((char *)init_nm0,EEPROM_WIFI_NM0, EEPROM_INT);
    SetEepromVal((char *)init_nm1,EEPROM_WIFI_NM1, EEPROM_INT);
    SetEepromVal((char *)init_nm2,EEPROM_WIFI_NM2, EEPROM_INT);
    SetEepromVal((char *)init_nm3,EEPROM_WIFI_NM3, EEPROM_INT);
    SetEepromVal((char *)init_gw0,EEPROM_WIFI_GW0, EEPROM_INT);
    SetEepromVal((char *)init_gw1,EEPROM_WIFI_GW1, EEPROM_INT);
    SetEepromVal((char *)init_gw2,EEPROM_WIFI_GW2, EEPROM_INT);
    SetEepromVal((char *)init_gw3,EEPROM_WIFI_GW3, EEPROM_INT);
    SetEepromVal((char *)init_ap0,EEPROM_WIFI_AP0, EEPROM_INT);
    SetEepromVal((char *)init_ap1,EEPROM_WIFI_AP1, EEPROM_INT);
    SetEepromVal((char *)init_ap2,EEPROM_WIFI_AP2, EEPROM_INT);
    SetEepromVal((char *)init_ap3,EEPROM_WIFI_AP3, EEPROM_INT);
    SetEepromVal((char *)init_bk,EEPROM_MQTT_BK, EEPROM_CHR);
    SetEepromVal((char *)init_ci,EEPROM_MQTT_CI, EEPROM_CHR);
    SetEepromVal((char *)init_un,EEPROM_MQTT_UN, EEPROM_CHR);
    SetEepromVal((char *)init_pw,EEPROM_MQTT_PW, EEPROM_CHR);
    AddMAC("mqtt_rx_", topic);
    SetEepromVal((char *)topic,EEPROM_MQTT_RT, EEPROM_CHR);
    AddMAC("mqtt_tx_", topic);
    SetEepromVal((char *)topic,EEPROM_MQTT_TT, EEPROM_CHR);
    SetEepromVal((char *)init_port,EEPROM_SVR_PORT, EEPROM_INT16);
    SetEepromVal((char *)init_bd,EEPROM_SER_BAUD, EEPROM_INT24);
    SetEepromVal((char *)init_sr,EEPROM_SER_SERV, EEPROM_CHR);
    SetEepromVal((char *)init_mqpwen,EEPROM_MQTTPWEN, EEPROM_CHR);
    SetEepromVal((char *)init_mqsslen,EEPROM_MQTTSSLEN, EEPROM_CHR);
    SetEepromVal((char *)init_mqclen,EEPROM_MQTTCLEN, EEPROM_CHR);
    SetEepromVal((char *)init_mqtt_port,EEPROM_MQTT_PT, EEPROM_INT16);
    SetEepromVal((char *)init_fota_port,EEPROM_FOTA_PT, EEPROM_INT16);
    SetEepromVal((char *)init_fota_pass,EEPROM_FOTA_PW, EEPROM_CHR);
   
    EEPROM.commit();
}

/********************************************************
 * Prints a HEX map of EEPROM Values
 * Function: printEEprom(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void printEEprom(void) {
    int i,j,v;
    for(j=0;j<32;j++) {
        Serial.print(j*16,HEX);
        Serial.print(F(" "));
        for(i=0;i<16;i++) {
            v = EEPROM.read(i+(16*j));
            Serial.print(v, HEX);
            Serial.print(" ");
        }
        Serial.println("");
    }   
}

/********************************************************
 * Update EEPROM ascii parameter
 * Function: SetEepromAscii(void *arg, int index, int eeprom_offset)
 * 
 * Parameter     Description
 * ---------     ---------------------------------------
 * *arg          pointer to input parameter structure
 * index         Parameter index
 * eeprom_offset Parameter EEPROM offset
 * return        no return value
 ********************************************************/
void SetEepromAscii(void *arg, int index, int eeprom_offset) 
{
    struct URL_Param *pURL_Param = (URL_Param *)arg;
    int len = os_strlen(pURL_Param->pParVal[index]);
    for(int i=0;i<len;i++) {                                                    
        EEPROM.write(eeprom_offset+i, (uint8_t) pURL_Param->pParVal[index][i]); //Update EEPROM char value
    }
    EEPROM.write(eeprom_offset+len, (uint8_t) 0);                               //Null Terminate String
}
/********************************************************
 * Insert EEPROM configuration in webpage
 * Function: GetEepromCfg(String *page)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *page        pointer to webpage string
 * return       no return value
 ********************************************************/
void GetEepromCfg(String *page)
{
    *page = reinterpret_cast<const __FlashStringHelper *>(PAGE_NetCfg);
    String css = reinterpret_cast<const __FlashStringHelper *>(PAGE_Style_css);
    page->replace("ADDSTYLE",css); 
    css = "";
    /*
    String js = reinterpret_cast<const __FlashStringHelper *>(PAGE_jQuery_js1);
    page->replace("JQUERY1",js); 
    js = "";
    */
    
    SetCfgPageWithEepromVal(page, "set_ssid", EEPROM_WIFISSID, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_pass", EEPROM_WIFIPASS, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_fota_pw", EEPROM_FOTA_PW, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_fota_pt", EEPROM_FOTA_PT, EEPROM_INT16);
    SetCfgPageWithEepromVal(page, "set_ip0", EEPROM_WIFI_IP0, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ip1", EEPROM_WIFI_IP1, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ip2", EEPROM_WIFI_IP2, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ip3", EEPROM_WIFI_IP3, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_nm0", EEPROM_WIFI_NM0, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_nm1", EEPROM_WIFI_NM1, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_nm2", EEPROM_WIFI_NM2, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_nm3", EEPROM_WIFI_NM3, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_gw0", EEPROM_WIFI_GW0, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_gw1", EEPROM_WIFI_GW1, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_gw2", EEPROM_WIFI_GW2, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_gw3", EEPROM_WIFI_GW3, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ap0", EEPROM_WIFI_AP0, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ap1", EEPROM_WIFI_AP1, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ap2", EEPROM_WIFI_AP2, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_ap3", EEPROM_WIFI_AP3, EEPROM_INT);
    SetCfgPageWithEepromVal(page, "set_bk", EEPROM_MQTT_BK, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_ci", EEPROM_MQTT_CI, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_un", EEPROM_MQTT_UN, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_pw", EEPROM_MQTT_PW, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_rt", EEPROM_MQTT_RT, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_tt", EEPROM_MQTT_TT, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_port", EEPROM_SVR_PORT, EEPROM_INT16);
    SetCfgPageWithEepromVal(page, "set_pt", EEPROM_MQTT_PT, EEPROM_INT16);
    SetCfgPageWithEepromVal(page, "set_pt", EEPROM_MQTT_CI, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_fota_pt", EEPROM_FOTA_PT, EEPROM_INT16);
    SetCfgPageWithEepromVal(page, "set_fota_pw", EEPROM_FOTA_PW, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_baud", EEPROM_SER_BAUD, EEPROM_INT24);
    SetCfgPageWithEepromVal(page, "set_serv", EEPROM_SER_SERV, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_mqtt_pw_en", EEPROM_MQTTPWEN, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_mqtt_ssl_en", EEPROM_MQTTSSLEN, EEPROM_CHR);
    SetCfgPageWithEepromVal(page, "set_mqtt_cl_en", EEPROM_MQTTCLEN, EEPROM_CHR);
}

/********************************************************
 * Insert EEPROM configuration in webpage
 * Function: SetCfgPageWithEepromVal(String *page, String par, int offset, int fmt)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *page        pointer to webpage string
 * par          token in webpage to replace
 * offset       parameter EEPROM offset
 * fmt          EEPROM data format
 * return       no return value
 ********************************************************/
void SetCfgPageWithEepromVal(String *page, String par, int offset, int fmt) {
    String SetVal ="";
    GetEepromVal(&SetVal, offset, fmt);
    page->replace(par,SetVal);   
}

/********************************************************
 * Get EEPROM Value
 * Function: GetEepromVal(String *val,int offset, int fmt)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *val         string to hold parameter value
 * offset       parameter EEPROM offset
 * fmt          EEPROM data format
 * return       no return value
 ********************************************************/
void GetEepromVal(String *val,int offset, int fmt) {
    int i;
    *val = "";
    switch(fmt) {
        case EEPROM_CHR:
            for(i=offset;i<(offset+32);i++) {
                if(EEPROM.read(i)!=0)  *val += char(EEPROM.read(i));
                else                   break;
             }
            break;
         case EEPROM_INT:
            *val = EEPROM.read(offset);
            break;
         case EEPROM_INT16:
            *val = EEPROM.read(offset+1) + (EEPROM.read(offset) * 256);
            break;
         case EEPROM_INT24:
            *val = EEPROM.read(offset+2) + (EEPROM.read(offset+1) * 256) + (EEPROM.read(offset+0) * 256 * 256);
            break;
    }
}

/********************************************************
 * Set EEPROM Value
 * Function: SetEepromVal(String *val,int offset, int fmt)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *val         string to hold parameter value
 * offset       parameter EEPROM offset
 * fmt          EEPROM data format
 * return       no return value
 ********************************************************/
void SetEepromVal(char *val,int offset, int fmt) {
    int i;
    switch(fmt) {
        case EEPROM_CHR:
            for(i=0;i<os_strlen(val);i++) {
                 EEPROM.write(offset+i, val[i]);
             }
             EEPROM.write(offset+os_strlen(val),(uint8_t) 0);
            break;
        case EEPROM_INT:
             EEPROM.write(offset, atoi(val));
             break;
        case EEPROM_INT16:
             EEPROM.write(offset+1, atoi(val)&0xFF);
             EEPROM.write(offset, (atoi(val)&0xFF00)>>8);
             break;
        case EEPROM_INT24:
             EEPROM.write(offset+2, atoi(val)&0xFF);
             EEPROM.write(offset+1, (atoi(val)&0xFF00)>>8);
             EEPROM.write(offset+0, (atoi(val)&0xFF0000)>>16);
             break;
    }
}

/********************************************************
 * Resets ESP8266 after delay set when callback is armed
 * Function: ResetEspTimerCallback(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void ResetEspTimerCallback(void) {
    ESP.reset();
}

/********************************************************
 * Initiates LED blink count-down
 * Function: blinkLed(int nblink)
 * extern "C" {} required so function can be called 
 * from endpoints.c file
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * nblink       number of times to blink LED
 * return       no return value
 ********************************************************/

//extern "C"{
    void blinkLed(int nblink) {
        blinkarg[0] = nblink;   //Number of blinks
        blinkarg[1] = 1;        //LED state to command
        blinkarg[2] = 1;        //First Callback Iteration
        os_timer_disarm(&BlinkLedTimer);
        os_timer_setfn(&BlinkLedTimer, (os_timer_func_t *)BlinkCallback, &blinkarg[0]);
        os_timer_arm(&BlinkLedTimer, 500, false);
    }
//}

/********************************************************
 * Callback for Blink timer
 * Function: BlinkCallback(int *pArg)
 * 
 * Parameter    Description
 * ---------    -----------------------------------------
 * *pArg        int - number of times to blink LED
 * *pArg+1      int - set LED state (0=off,1=on) 
 * *pArg+2      int - set to 1 first time to print header 
 * return       no return value
 ********************************************************/
void BlinkCallback(int *pArg) {
    int i;
    int nblinks,ledstate,start;
    nblinks = *(int *)pArg;
    ledstate = *(int *)(pArg+1);
    start = *(int *)(pArg+2);

    if(start == 1)
    {
        Serial.print(F("Blink countdown:"));
        blinkarg[2] = 0; //do not print header next time
    }

    if(ledstate==1)
    {
         Serial.print( nblinks);
         if(nblinks>1) 
         {
             Serial.print(F("."));
         }
         else
         {
             Serial.println(F("..."));
         }
         digitalWrite(LED_IND, HIGH);
         blinkarg[1] = 0;
         os_timer_arm(&BlinkLedTimer, 500, false);
    }
    else
    {
         digitalWrite(LED_IND, LOW);
         blinkarg[1] = 1;   //start with led on cmd
         if(--nblinks!=0) {
            --blinkarg[0];  //remaining blinks
            os_timer_arm(&BlinkLedTimer, 500, false); 
         }     
    }
}

/********************************************************
 * SDK API Web Server Initialization
 * Function: SdkWebServer_Init(int port)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * port         http server listen port
 * return       no return value
 ********************************************************/
void SdkWebServer_Init(int port) {
    LOCAL struct espconn esp_conn;
    LOCAL esp_tcp esptcp;
    //Fill the connection structure, including "listen" port
    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    esp_conn.proto.tcp->local_port = port;
    esp_conn.recv_callback = NULL;
    esp_conn.sent_callback = NULL;
    esp_conn.reverse = NULL;
    //Register the connection timeout(0=no timeout)
    espconn_regist_time(&esp_conn,0,0);
    //Register connection callback
    espconn_regist_connectcb(&esp_conn, SdkWebServer_listen);
    //Start Listening for connections
    espconn_accept(&esp_conn); 
    Serial.println("ESP8266 Mode: Web Server Running - Type = SDK API");
}

/********************************************************
 * SDK API Web Server TCP Client Connection Callback
 * Function: SdkWebServer_listen(void *arg)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * return       no return value
 ********************************************************/
void SdkWebServer_listen(void *arg)
{
    struct espconn *pesp_conn = ( espconn *)arg;

    espconn_regist_recvcb(pesp_conn, SdkWebServer_recv);
    espconn_regist_reconcb(pesp_conn, SdkWebServer_recon);
    espconn_regist_disconcb(pesp_conn, SdkWebServer_discon);
    espconn_regist_sentcb(pesp_conn, SdkWebServer_sent_cb);
}

void SdkWebServer_sent_cb(void *arg) {
    //Serial.println(F("html sent complete"));
    httpsend_ready = true;
}

/********************************************************
 * SDK API Web Server Receive Data Callback
 * Function: SdkWebServer_recv(void *arg, char *pusrdata, 
 *                          unsigned short length)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * *pusrdata    data received after IP:port
 * length       data length (bytes)
 * return       no return value
 ********************************************************/
void SdkWebServer_recv(void *arg, char *pusrdata, unsigned short length)
{
    URL_Param *pURL_Param = NULL;
    char *pParseBuffer = NULL;
    char * pRequest = NULL;
    char * pReply = NULL;
    
    bool parse_flag = false;

    ptrespconn = ( espconn *)arg;
    espconn_set_opt(ptrespconn, ESPCONN_REUSEADDR);

    parse_flag = SdkWebServer_savedata(pusrdata, length);
    pURL_Param = (URL_Param *)os_zalloc(sizeof(URL_Param));
    
    pRequest = (char *)os_zalloc(1024);
    pReply = (char *)os_zalloc(1024);
    os_memcpy(pRequest, precvbuffer, length);

    if(!arduino_server) {
        Serial.println(F("-------------------------------------"));
        Serial.println(F("HTTP Message Rx: [Code: SDK API]"));
        Serial.println(F("-------------------------------------"));
    }

    Server_ProcessRequest(SVR_HTTP_SDK, pRequest, pReply);

    //Clean up the memory
    if (precvbuffer != NULL)
    {
        os_free(precvbuffer);
        precvbuffer = NULL;
    }
    os_free(pURL_Param);
    os_free(pRequest);
    os_free(pReply);
    pURL_Param = NULL;
    pRequest=NULL;
    pReply=NULL;
}

void Server_SendReply(int servertype, int replytype, String payld) {
    // Now send reply
    if(!arduino_server) {
        Serial.println(F("-------------------------------------"));
        Serial.println(F("Reply Payload:"));
        Serial.println(payld);
        Serial.println(F("-------------------------------------"));
    }
    switch(servertype)
    {
        case SVR_MQTT:
            client->publish((char *)mqtt_tt.c_str(), payld.c_str(), false);
            break;
        case SVR_HTTP_LIB:
            //httpClient.print(payld);
            break;
        case SVR_HTTP_SDK:
            switch(replytype) {
                case REPLY_JSON:
                    SdkWebServer_senddata(ptrespconn, true, (char *)payld.c_str(),"application/json");
                    break;
                case REPLY_TEXT:
                    SdkWebServer_senddata(ptrespconn, true, (char *)payld.c_str(),"text/html");
                    break;
            }
            break;
        case SVR_COAP:    //endpoint api returns reply
            break;    
    }
}
/******************************************************************************************
 * Get Request & Process 
 ******************************************************************************************/
 void Server_ProcessRequest(int servertype, char *payload, char *reply)
 {
    int i,nblink;
    String payld = "";
    String ArduinoRequest = "";
    String WebPage = "";
    RQST_Param rparam;
    URL_Param *pURL_Param = NULL;
    pURL_Param = (URL_Param *)os_zalloc(sizeof(URL_Param));
    SdkWebServer_parse_url_params(payload, pURL_Param);
    
    switch (pURL_Param->Type) {
        case GET:
            // ----------------------------------------------------------------------------
            // Serving Requests
            // ----------------------------------------------------------------------------

            if(os_strcmp(pURL_Param->pParam[0], "request")==0) {
                // GetSensors is 1 of 6 requests the server currently supports
                if(os_strcmp(pURL_Param->pParVal[0], "GetSensors")==0) {
                      rparam.request = Get_SENSORS;
                      rparam.requestval = 0;
                      // Execute request & get reply string
                      Server_ExecuteRequest(servertype, rparam, &payld,"");
                }
                // LedOn is 2 of 6 requests the server currently supports
                else if(os_strcmp(pURL_Param->pParVal[0], "LedOn")==0) {
                      rparam.request = SET_LED_ON;
                      rparam.requestval = 0;
                      // Execute request & get reply string
                      Server_ExecuteRequest(servertype, rparam, &payld,"");
                }
                // LedOff is 3 of 6 requests the server currently supports
                else if(os_strcmp(pURL_Param->pParVal[0], "LedOff")==0) {
                      rparam.request = SET_LED_OFF;
                      rparam.requestval = 0;
                      // Execute request & get reply string
                      Server_ExecuteRequest(servertype, rparam, &payld,"");
                }
                // BlinkLed is 4 of 6 requests the server currently supports
                else if(os_strcmp(pURL_Param->pParVal[0], "BlinkLed")==0) {
                      rparam.request = BLINK_LED;
                      if(os_strcmp(pURL_Param->pParam[1], "nblink")==0) {
                          rparam.requestval = atoi(pURL_Param->pParVal[1]);
                      }
                      else
                      {
                          rparam.requestval = 1;
                      }
                      // Execute request & get reply string
                      Server_ExecuteRequest(servertype, rparam, &payld,"");
                }
                // HttpOn is 5 of 6 requests the server currently supports
                else if(os_strcmp(pURL_Param->pParVal[0], "HttpOn")==0) {
                      rparam.request = SET_HTTP_ON;
                      rparam.requestval = 0;
                      // Execute request & get reply string
                      Server_ExecuteRequest(servertype, rparam, &payld,"");
                      //Turn SSL Off so http server can run
                      SetEepromVal((char *)"false",EEPROM_MQTTSSLEN, EEPROM_CHR);  //Turn MQTT SSL Off
                      SetEepromVal((char *)"false",EEPROM_MQTTCLEN, EEPROM_CHR);   //Turn MQTT Client Off                        
                      EEPROM.commit();
                      //Restart 
                      WebPage = reinterpret_cast<const __FlashStringHelper *>(PAGE_WaitAndReset);
                      SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
                      os_timer_arm(&ResetEspTimer, 3000, false); 
                }
                // MqttSslOn is 6 of 6 requests the server currently supports
                else if(os_strcmp(pURL_Param->pParVal[0], "MqttSslOn")==0) {
                      rparam.request = SET_MQTTSSL_ON;
                      rparam.requestval = 0;
                      // Execute request & get reply string
                      Server_ExecuteRequest(servertype, rparam, &payld,"");
                      //Turn SSL Off so http server can run
                      SetEepromVal((char *)"true",EEPROM_MQTTSSLEN, EEPROM_CHR);  //Turn MQTT SSL Off
                      SetEepromVal((char *)"true",EEPROM_MQTTCLEN, EEPROM_CHR);   //Turn MQTT Client Off                        
                      EEPROM.commit();
                      //Restart 
                      WebPage = reinterpret_cast<const __FlashStringHelper *>(PAGE_WaitAndReset);
                      SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
                      os_timer_arm(&ResetEspTimer, 3000, false); 
                }
                // Add additional requests here
                else    //Invalid request
                {
                    rparam.request == INVALID_REQUEST;
                    payld = "Invalid Request";                    
                }
               // Now send reply
                Server_SendReply(servertype, REPLY_JSON, payld);
            }
            // ----------------------------------------------------------------------------
            // Serving Web Pages
            // ----------------------------------------------------------------------------
            //
            // ------------- Load Test Page -----------------------------------------------
            //NOTE: From this, a 1927 byte limit for serving web-pages to external domains
            //      No such limit exists for local network access
            if(os_strcmp(pURL_Param->pParam[0], "test")==0) {
                /***************
                WebPage = reinterpret_cast<const __FlashStringHelper *>(PAGE_NetCfg2);
                String css = reinterpret_cast<const __FlashStringHelper *>(PAGE_Style2_css);
                WebPage.replace("ADDSTYLE",css); 
                css = "";
                SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
                *******************/
                //Disable MQTT client object & Restart

                //Restart 
                WebPage = reinterpret_cast<const __FlashStringHelper *>(PAGE_WaitAndReset);
                SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
                os_timer_arm(&ResetEspTimer, 3000, false); 
             }
            // ------------- Load Config Page ---------------------------------------------
            if(os_strcmp(pURL_Param->pParam[0], "config")==0) {                
                GetEepromCfg(&WebPage);
                SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
             }
            // -------------- Save config or Reset ESP8266 --------------------------------
            if(os_strcmp(pURL_Param->pParam[0], "ssid")==0) {
                
                if(os_strcmp(pURL_Param->pParVal[0], "reset")==0){
                    // ------------- Reset ESP8266 ----------------------------------------
                    WebPage = reinterpret_cast<const __FlashStringHelper *>(PAGE_WaitAndReset);
                    SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
                    os_timer_arm(&ResetEspTimer, 3000, false); 
                }
                else {
                    // -------------- Save config to EEPROM and reload config page --------
                    SetEepromCfg((void *)pURL_Param);        
                    WebPage = reinterpret_cast<const __FlashStringHelper *>(PAGE_WaitAndReload); //reload config page
                    SdkWebServer_senddata(ptrespconn, true, (char *) WebPage.c_str(),"text/html");
                }
            }
            // ----------------------------------------------------------------------------
            // Serving Arduino via serial port
            // ----------------------------------------------------------------------------
            if(os_strcmp(pURL_Param->pParam[0], "arduino")==0) {
                active_svr_rqst = servertype;
                //-------------- Request = SetDigital ---------------------
                if(os_strcmp(pURL_Param->pParVal[0], "SetDigital")==0){
                    if(os_strcmp(pURL_Param->pParam[1], "chan")==0) {
                        ArduinoRequest = "Arduino_SD";
                        ArduinoRequest += pURL_Param->pParVal[1];
                    }
                    else {
                        ArduinoRequest = "Invalid Request";  
                    }
                    if((os_strcmp(pURL_Param->pParam[2], "state")==0)&&(os_strcmp(ArduinoRequest.c_str(), "Invalid Request")!=0)) {
                        ArduinoRequest += pURL_Param->pParVal[2];
                    }
                    else {
                        ArduinoRequest = "Invalid Request";  
                    }
                }
                //-------------- Request = GetDigital ----------------------
                else if(os_strcmp(pURL_Param->pParVal[0], "GetDigital")==0){
                    if(os_strcmp(pURL_Param->pParam[1], "chan")==0) {
                        ArduinoRequest = "Arduino_GD";
                        ArduinoRequest += pURL_Param->pParVal[1];
                    }
                    else {
                        ArduinoRequest = "Invalid Request";  
                    }
                }
                //-------------- Request = GetAnalog ----------------------
                else if(os_strcmp(pURL_Param->pParVal[0], "GetAnalog")==0){
                    if(os_strcmp(pURL_Param->pParam[1], "chan")==0) {
                        ArduinoRequest = "Arduino_GA";
                        ArduinoRequest += pURL_Param->pParVal[1];
                    }
                    else {
                        ArduinoRequest = "Invalid Request";  
                    }
                }
                //-------------- Request = Send MQTT Msg -------------------
                else if(os_strcmp(pURL_Param->pParVal[0], "SendMsg")==0){
                    if(os_strcmp(pURL_Param->pParam[1], "msg")==0) {
                        ArduinoRequest = "Arduino_SM";
                        ArduinoRequest += pURL_Param->pParVal[1];
                    }
                    else {
                        ArduinoRequest = "Invalid Request";  
                    }
                }
                //-------------- Request is not recognized -----------------
                else {
                    ArduinoRequest = "Invalid Request";  
                }
                //------------- Send "invalid request" as reply ------------
                if(os_strcmp(ArduinoRequest.c_str(), "Invalid Request")==0) {
                    payld = ArduinoRequest;
                    Server_SendReply(servertype, REPLY_TEXT, payld);
                }
                //------------- Execute valid request  ---------------------
                else {
                    rparam.request = ARDUINO_REQUEST;
                    rparam.requestval = 0;
                    Server_ExecuteRequest(servertype, rparam, &payld, ArduinoRequest);
                }
            }
            break;

        case POST:
            Serial.print(F("We have a POST request.\n"));
            break;
    }
    strcpy(reply,(char *)payld.c_str());
    os_free(pURL_Param);
    pURL_Param = NULL;
    payld = "";
    ArduinoRequest = "";
    WebPage = "";
 }

/********************************************************
 * SDK API Web Server TCP Connection Closed Callback
 * Function: SdkWebServer_discon(void *arg)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * return       no return value
 ********************************************************/
void SdkWebServer_discon(void *arg)
{
    struct espconn *pesp_conn = ( espconn *)arg;

    os_printf("webserver's %d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
            pesp_conn->proto.tcp->remote_ip[1],pesp_conn->proto.tcp->remote_ip[2],
            pesp_conn->proto.tcp->remote_ip[3],pesp_conn->proto.tcp->remote_port);
}

/********************************************************
 * SDK API Web Server TCP Disconnect on error Callback
 * Function: SdkWebServer_recon(void *arg, sint8 err)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * err          error code
 * return       no return value
 ********************************************************/
void SdkWebServer_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = ( espconn *)arg;

    os_printf("webserver's %d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
        pesp_conn->proto.tcp->remote_ip[1],pesp_conn->proto.tcp->remote_ip[2],
        pesp_conn->proto.tcp->remote_ip[3],pesp_conn->proto.tcp->remote_port, err);
}

void SdkWebServer_senddata(void *arg, bool responseOK, char *psend, char* contenttype)
{
    uint16 length = 0;
    uint16 length_send = 0;
    uint16 length_sent = 0;
    char *pbuf = NULL;
    char httphead[512];
    struct espconn *ptrespconn = ( espconn *)arg;
    os_memset(httphead, 0, 512);
    String gmt,expire;

    time_t now = time(nullptr);
    gmt = ctime(&now);
    now += 3600 * 24;  //Expires in 1 day
    expire = ctime(&now);
    gmt = gmt.substring(0,3) + "," + gmt.substring(7,10) + gmt.substring(3,7) + gmt.substring(19,24) + gmt.substring(10,19) + " GMT";
    expire = expire.substring(0,3) + "," + expire.substring(7,10) + expire.substring(3,7) + expire.substring(19,24) + expire.substring(10,19) + " GMT";
    
    if (responseOK) {
        os_sprintf(httphead,
                   "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nServer: lwIP/1.4.0\r\nAccess-Control-Allow-Origin: *\r\nDate: %s\r\n",
                   psend ? os_strlen(psend) : 0,gmt.c_str());

        if (psend) {
            os_sprintf(httphead + os_strlen(httphead),
                   "Content-type: %s\r\nExpires: %s\r\nPragma: no-cache\r\n\r\n",contenttype,expire.c_str());
                   //"Content-type: text/html\r\nExpires: %s\r\nPragma: no-cache\r\n\r\n",contenttype,expire.c_str());
            length = os_strlen(httphead) + os_strlen(psend);
            pbuf = (char *)os_zalloc(length + 1);
            os_memcpy(pbuf, httphead, os_strlen(httphead));
            os_memcpy(pbuf + os_strlen(httphead), psend, os_strlen(psend));
        } else {
            os_sprintf(httphead + os_strlen(httphead), "\n");
            length = os_strlen(httphead);
        }
    } else {
        os_sprintf(httphead, "HTTP/1.0 400 BadRequest\r\n\
Content-Length: 0\r\nServer: lwIP/1.4.0\r\n\n");
        length = os_strlen(httphead);
    }
    if (psend) {
         espconn_send(ptrespconn, (uint8 *)pbuf, length);
    } else {
        //httpsend_ready set in SdkWebServer_sent_cb() callback which is called when espconn_send() completes
        if(httpsend_ready) {
            httpsend_ready = false;
            espconn_send(ptrespconn, (uint8 *)httphead, length);
        }
    }

    if (pbuf) {
        os_free(pbuf);
        pbuf = NULL;
    }
}

/********************************************************
 * SDK API Web Server save data from connected client
 * Function: bool SdkWebServer_savedata(char *precv, 
 *                                      uint16 length)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *precv       string received
 * length       string length
 * return       true if succes ful save
 ********************************************************/
bool SdkWebServer_savedata(char *precv, uint16 length)
{
    bool flag = false;
    char length_buf[10] = {0};
    char *ptemp = NULL;
    char *pdata = NULL;
    uint16 headlength = 0;
    static uint32 totallength = 0;

    ptemp = (char *)os_strstr(precv, "\r\n\r\n");

    if (ptemp != NULL) {
        length -= ptemp - precv;
        length -= 4;
        totallength += length;
        headlength = ptemp - precv + 4;
        pdata = (char *)os_strstr(precv, "Content-Length: ");

        if (pdata != NULL) {
            pdata += 16;
            precvbuffer = (char *)os_strstr(pdata, "\r\n");

            if (precvbuffer != NULL) {
                os_memcpy(length_buf, pdata, precvbuffer - pdata);
                dat_sumlength = atoi(length_buf);
            }
        } else {
          if (totallength != 0x00){
            totallength = 0;
            dat_sumlength = 0;
            return false;
          }
        }
        if ((dat_sumlength + headlength) >= 1024) {
          precvbuffer = (char *)os_zalloc(headlength + 1);
            os_memcpy(precvbuffer, precv, headlength + 1);
        } else {
          precvbuffer = (char *)os_zalloc(dat_sumlength + headlength + 1);
          os_memcpy(precvbuffer, precv, os_strlen(precv));
        }
    } else {
        if (precvbuffer != NULL) {
            totallength += length;
            os_memcpy(precvbuffer + os_strlen(precvbuffer), precv, length);
        } else {
            totallength = 0;
            dat_sumlength = 0;
            return false;
        }
    }

    if (totallength == dat_sumlength) {
        totallength = 0;
        dat_sumlength = 0;
        return true;
    } else {
        return false;
    }
}

/********************************************************
 * SDK API Web Server parse received parameters
 * Function: void SdkWebServer_ parse_url_params(
 *           char *precv, URL_Param *purl_param) 
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *precv       string received
 * *purl_param  parsed parameter structure
 * return       no return value
 ********************************************************/
void SdkWebServer_parse_url_params(char *precv, URL_Param *purl_param)
  {
    char *str = NULL;
    unsigned int length = 0;
    char *pbuffer = NULL;
    char *pbufer = NULL;
    int ipar=0;

    if (purl_param == NULL || precv == NULL) {
        return;
    }

    //pbuffer = (char *)os_strstr(precv, "Host:");
    //pbuffer = (char *)os_strstr(precv, "Accept:");
    pbuffer = (char *)os_strstr(precv, "Connection:");

    if (pbuffer != NULL) {
        length = pbuffer - precv;
        pbufer = (char *)os_zalloc(length + 1);
        pbuffer = pbufer;
        os_memcpy(pbuffer, precv, length);
        os_memset(purl_param->pParam, 0, URLSize*URLSize);
        os_memset(purl_param->pParVal, 0, URLSize*URLSize);

        if (os_strncmp(pbuffer, "GET /favicon.ico", 16) == 0) {
            purl_param->Type = GET_FAVICON;
            os_free(pbufer);
            return;
        } else if (os_strncmp(pbuffer, "GET ", 4) == 0) {
          purl_param->Type = GET;
            pbuffer += 4;
        } else if (os_strncmp(pbuffer, "POST ", 5) == 0) {
          purl_param->Type = POST;
            pbuffer += 5;
        }

        pbuffer ++;
        str = (char *)os_strstr(pbuffer, "?");

        if (str != NULL) {
            str ++;
            do {
                pbuffer = (char *)os_strstr(str, "=");
                length = pbuffer - str;
                os_memcpy(purl_param->pParam[ipar], str, length);
                str = (char *)os_strstr(++pbuffer, "&");
                if(str != NULL) {
                    length = str - pbuffer;
                    os_memcpy(purl_param->pParVal[ipar++], pbuffer, length);
                    str++;
                }
                else {
                    str = (char *)os_strstr(pbuffer, " HTTP");
                    if(str != NULL) {
                        length = str - pbuffer;
                        os_memcpy(purl_param->pParVal[ipar++], pbuffer, length);
                        str = NULL;
                    }
                    else {
                        //os_memcpy(purl_param->pParVal[ipar++], pbuffer, 16);
                    }
                }
            }
            while (str!=NULL);
        }
        else {
            str = (char *)os_strstr(pbuffer, "/");
            if (str != NULL) {
                str ++;

                str = (char *)os_strstr(pbuffer, " HTTP");
                if(str != NULL) {
                    length = str - pbuffer;
                    os_memcpy(purl_param->pParVal[ipar], "0", 1);
                    os_memcpy(purl_param->pParam[ipar++], pbuffer, length);
                    str = NULL;
                }
                else {
                    //os_memcpy(purl_param->pParVal[ipar++], pbuffer, 16);
                }
            }
        }

        purl_param->nPar = ipar;
        os_free(pbufer);
    } else {
        return;
    }
}


/********************************************************
 * print status to serial port
 * Function: util_printStatus(char * status, int s)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *status      status string
 * s            status code
 * return       no return value
 ********************************************************/

void util_printStatus(char * status, int s) {
      Serial.print(system_get_free_heap_size());
      delay(100);
      Serial.print(" ");
      delay(100);
      Serial.print(millis()/1000);
      delay(100);
      Serial.print(" ");
      delay(100);
      if(s>=0) Serial.print(s);
      else Serial.print("");
      delay(100);
      Serial.print(" ");
      delay(100);
      Serial.println(status);
//    }
    delay(100);
}

/********************************************************
 * connect to local Wifi
 * Function: util_startWIFI(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void util_startWIFI(void) {
    if ((WiFi.status() != WL_CONNECTED)&&(wifi!=0)) {
        int itimeout=0;
        String EepromSsid="";
        String EepromPass="";
        IPAddress ip = WiFi.localIP();
        //Get IP, NM, GW from EEPROM
        const IPAddress ip_eeprom(EEPROM.read(EEPROM_WIFI_IP0),EEPROM.read(EEPROM_WIFI_IP1),EEPROM.read(EEPROM_WIFI_IP2),EEPROM.read(EEPROM_WIFI_IP3)); 
        const IPAddress nm_eeprom(EEPROM.read(EEPROM_WIFI_NM0),EEPROM.read(EEPROM_WIFI_NM1),EEPROM.read(EEPROM_WIFI_NM2),EEPROM.read(EEPROM_WIFI_NM3)); 
        const IPAddress gw_eeprom(EEPROM.read(EEPROM_WIFI_GW0),EEPROM.read(EEPROM_WIFI_GW1),EEPROM.read(EEPROM_WIFI_GW2),EEPROM.read(EEPROM_WIFI_GW3)); 
        const IPAddress ap_eeprom(EEPROM.read(EEPROM_WIFI_AP0),EEPROM.read(EEPROM_WIFI_AP1),EEPROM.read(EEPROM_WIFI_AP2),EEPROM.read(EEPROM_WIFI_AP3));
        GetEepromVal(&EepromSsid, EEPROM_WIFISSID, EEPROM_CHR); 
        GetEepromVal(&EepromPass, EEPROM_WIFIPASS, EEPROM_CHR);  
             
        if(!arduino_server) {
            Serial.print(F("\nLocal ESP   IP:"));
            delay(10);
            Serial.println(ip);
            Serial.print(F("From EEPROM IP:"));
            delay(10);
            Serial.println(ip_eeprom);
            delay(10);
            Serial.print(F("From EEPROM NM:"));
            delay(10);
            Serial.println(nm_eeprom);
            delay(10);
            Serial.print(F("From EEPROM GW:"));
            delay(10);
            Serial.println(gw_eeprom);
            delay(10);
            Serial.print(F("From EEPROM AP:"));
            delay(10);
            Serial.println(ap_eeprom);
            delay(10);
        }
        if( ip!= ip_eeprom) {  //Set IP if current not equal to EEPROM value
            WiFi.config(ip_eeprom, gw_eeprom, nm_eeprom);  
            if(!arduino_server) {
                Serial.println();
                delay(10);
                Serial.print(F("ESP8266 IP:"));
                delay(10);
                Serial.println(ip_eeprom);
                delay(10);
                Serial.print(F("Fixed   IP:"));
                delay(10);
                Serial.println(ip_eeprom);
                delay(10);
                Serial.print(F("IP now set to: "));
                delay(10);
                Serial.println(WiFi.localIP());
                delay(10);
           }
        }
        // Connect to WiFi network
        if(os_strlen(EepromSsid.c_str())>0) {
            strcpy((char *)init_ssid,EepromSsid.c_str());
        }
        if(os_strlen(EepromPass.c_str())>0) {
            strcpy((char *)init_pass,EepromPass.c_str());
        }
        if(!arduino_server) {
            Serial.println();
            delay(10);
            Serial.println();
            delay(10);
            Serial.print(F("Connecting to "));
            delay(10);
            Serial.print(init_ssid);
            delay(10); 
        }
        
        wifi_station_connect();
        WiFi.begin(init_ssid, init_pass);
        
        while ((WiFi.status() != WL_CONNECTED)&&(itimeout++<20)) {  //10 sec timeout
            if(!arduino_server) {
                Serial.print(".");
            }
            delay(500);
        }
        if(!arduino_server) {
            Serial.println("");
        }
        if(itimeout<20) {
            if(!arduino_server) {
                Serial.println(F("WiFi connected"));
                // Print the IP address
                Serial.print("ESP8266 IP: ");
                Serial.println(WiFi.localIP());
              
                Serial.print(F("ESP8266 WebServer Port: "));
                Serial.println(http_port);
            }
            wifi=1;
        }
        else {
            //unsuccessful connecting to wifi for > 10 sec, initialize AP mode
            Serial.println("Could not connect to wifi, initializing AP ");
            strcat((char *)init_ssid,"_ESP_AP");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAPConfig(ap_eeprom, ap_eeprom, nm_eeprom); 
            WiFi.softAP(init_ssid);
            // Print the IP address
            Serial.print("ESP8266 IP: ");
            Serial.println(WiFi.softAPIP());
            wifi=0;
       }
        delay(300);    
    }
    //init_GmtTime();   // Get GMT Time       
}
/********************************************************
 * setup local Wifi as AP
 * Function: setup_wifi_ap_mode(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void setup_wifi_ap_mode(void)
{
    //char ssid[33];
    //char password[33];
    //wifi_set_opmode(SOFTAP_MODE);
    wifi_set_opmode(STATIONAP_MODE);
    wifi_softap_dhcps_stop();

    struct softap_config apconfig;
    if(wifi_softap_get_config(&apconfig))
    {
        os_memset(apconfig.ssid, 0, sizeof(apconfig.ssid));
        os_memset(apconfig.password, 0, sizeof(apconfig.password));

        strcat((char *)init_ssid,"_AP");
        strcpy((char *)apconfig.ssid,init_ssid);

        apconfig.authmode = AUTH_OPEN;
        apconfig.ssid_hidden = 0;
        apconfig.max_connection = 4;
        if(!wifi_softap_set_config(&apconfig))
        {
            Serial.println(F("ESP8266 not set ap config!\r\n"));
        }
    }
    //Set WiFi event callback
    wifi_set_event_handler_cb(wifi_event_cb);

    LOCAL struct ip_info info;
    IP4_ADDR(&info.ip, 192, 168, 4, 1);
    IP4_ADDR(&info.gw, 192, 168, 4, 1);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
    wifi_set_ip_info(SOFTAP_IF, &info);

    struct dhcps_lease dhcp_lease;
    IP4_ADDR(&dhcp_lease.start_ip, 192, 168, 4, 2);
    IP4_ADDR(&dhcp_lease.end_ip, 192, 168, 4, 5);
    wifi_softap_set_dhcps_lease(&dhcp_lease);

    wifi_softap_dhcps_start();

    //Startup Complete Status
    Serial.println("Startup Complete...");
}

/********************************************************
 * Callback when External Station Connects or Disconnects
 * Function: void wifi_event_cb(System_Event_t *evt)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *evt         event structure
 * return       no return value
 ********************************************************/
void wifi_event_cb(System_Event_t *evt) {
  struct ip_info ipConfig;
  static int serverinit=0;
  switch (evt->event) {
    case EVENT_SOFTAPMODE_STACONNECTED:
      Serial.print(F("station: "));
      Serial.print(MACSTR);
      Serial.println(F(" join, AID = "));
      //Start Web Server (Upon first connection)
      if(!serverinit) {
        SdkWebServer_Init(http_port);
        serverinit=1;
      }
      break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
      Serial.print(F("station: "));
      Serial.print(MACSTR);
      Serial.println(F(" leave, AID = "));
      break;
    default:
      break;
  }
}

/********************************************************
 * Read 1 sensor each time function is called
 * Current sensors: ESP8266 ADC with 8-1 MUX input
 * Function: void ReadSensors(int interval)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * interval     milliseconds (minimum) between reads
 * return       no return value
 ********************************************************/
void ReadSensors(int interval) {
    yield();
    long now = millis();                 
    if (now - lastMsg > interval) {  // Read 1 sensor every "interval" milliseconds or longer
        lastMsg = now;
    }
    else {
        return;
    }
    switch(state++) {
        case 0:
            //Set 8-1 amux to position 0
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
            //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain0, 2);
            break;
        case 1:
            //Set 8-1 amux to position 1
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain1, 2);
            break;
        case 2:
            //Set 8-1 amux to position 2
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain2, 2);
            break;
        case 3:
            //Set 8-1 amux to position 3
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain3, 2);
            break;
        case 4:
            //Set 8-1 amux to position 4
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain4, 2);
            break;
        case 5:
            //Set 8-1 amux to position 5
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain5, 2);
            break;
        case 6:
            //Set 8-1 amux to position 6
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain6, 2);
            break;
        case 7:
            //Set 8-1 amux to position 7
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain7, 2);
            state = 0;
            break;
        default:
            break;
    }
    ESP.wdtFeed(); 
    yield();
}

/********************************************************
 * add key/value entry into json string
 * Function: jsonAdd( 
 *                      String * s, 
 *                      String key, 
 *                      String val)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *s           pointer to current json string
 * key          this json string key
 * val          this json string value
 * return       no return value
 ********************************************************/
void jsonAdd(String *s, String key,String val) {
    *s += '"' + key + '"' + ":" + '"' + val + '"';
}

/********************************************************
 * encode key/value entry into json string
 * Function: jsonEncode(int pos, 
 *                      String * s, 
 *                      String key, 
 *                      String val)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * pos          position of this json entry
 * *s           pointer to current json string
 * key          this json string key
 * val          this json string value
 * return       no return value
 ********************************************************/
void jsonEncode(int pos, String * s, String key, String val) {
    switch (pos) {
      case ONEJSON:      
      case FIRSTJSON:
        *s += "{\r\n";
        jsonAdd(s,key,val);
        *s+= (pos==ONEJSON) ? "\r\n}" : ",\r\n";
        break;
      case NEXTJSON:    
        jsonAdd(s,key,val);
        *s+= ",\r\n";
         break;
      case LASTJSON:    
        jsonAdd(s,key,val);
        *s+= "\r\n}";
        break;
    }
}

/********************************************************
 * Create request reply string to request
 * Function: Server_ExecuteRequest(int servertype, RQST_Param rparam, String* reply, String ArduinoRequest)
 * 
 * Parameter      Description
 * ---------      ---------------------------------------
 * servertype     server type (SDK or LIB)
 * RQST_Param     structure
 *  request       request enumberation
 *  requestval    value associated with request - such as value to set
 * *reply         reply string pointer
 * ArduinoRequest Arduino request string pointer
 ********************************************************/
void Server_ExecuteRequest(int servertype, RQST_Param rparam, String* reply, String ArduinoRequest) {
    String v = "";
    if(servertype == SVR_HTTP_LIB) {
        // Prepare Response header
        *reply  = "HTTP/1.1 200 OK\r\n";
        *reply  += "Access-Control-Allow-Origin: *\r\n";
        ESP.wdtFeed();
    }
    switch (rparam.request) {
        case SET_LED_OFF:
        case SET_LED_ON:
            // Set GPIOn according to the request
            digitalWrite(LED_IND , rparam.request); //SET_LED_OFF=0.SET_LED_ON=1
            // Prepare the response for GPIO state
            switch(servertype) {
                case SVR_HTTP_LIB:            
                    *reply  += "Content-Type: text/html\r\n\r\n";
                    *reply  += "<!DOCTYPE HTML>\r\nLED is now ";
                    *reply  += (rparam.request)?"ON":"OFF";
                    *reply  += "</html>\n";
                    break;
                case SVR_COAP:            
                case SVR_MQTT:            
                case SVR_HTTP_SDK:
                    *reply  += "LED is now ";
                    *reply  += (rparam.request)?"ON":"OFF";
                    *reply  += "\n";
                    break;
            }
            break;
       case BLINK_LED:
            blinkLed(rparam.requestval); //Blink Led requestval times using non-blocking timer
            // Prepare the response for LED Blinking state
            switch(servertype) {
                case SVR_HTTP_LIB:            
                    *reply  += "Content-Type: text/html\r\n\r\n";
                    *reply  += "<!DOCTYPE HTML>\r\nLED is now blinking ";
                    *reply  += rparam.requestval;
                    *reply  += " times.";
                    *reply  += "</html>\n";
                    break;
                case SVR_COAP:            
                case SVR_MQTT:            
                case SVR_HTTP_SDK:
                    *reply  += "LED is now blinking ";
                    *reply  += rparam.requestval;
                    *reply  += " times.";
                    *reply  += "\n";
                    break;
            }
            break;
       case Get_SENSORS:
            //Create JSON return string
            if(servertype == SVR_HTTP_LIB) {
                *reply  += "Content-Type: application/json\r\n\r\n";
            }
            jsonEncode(FIRSTJSON,reply ,"Ain0", Ain0);
            jsonEncode(NEXTJSON,reply ,"Ain1", Ain1);
            jsonEncode(NEXTJSON,reply ,"Ain2", Ain2);
            jsonEncode(NEXTJSON,reply ,"Ain3", Ain3);
            jsonEncode(NEXTJSON,reply ,"Ain4", Ain4);
            jsonEncode(NEXTJSON,reply ,"Ain5", Ain5);
            jsonEncode(NEXTJSON,reply ,"Ain6", Ain6);
            jsonEncode(NEXTJSON,reply ,"Ain7", Ain7);
            v = system_get_free_heap_size();
            jsonEncode(NEXTJSON,reply ,"SYS_Heap", v);
            v = millis()/1000;
            jsonEncode(LASTJSON,reply ,"SYS_Time", v);
            break;
       case SET_HTTP_ON:
            *reply  += "HTTP Server Restored and MQTT Disabled\n";
            break;
       case SET_MQTTSSL_ON:
            *reply  += "MQTT SSL Broker Connection Restored and HTTP Disabled\n";
            break;
       case ARDUINO_REQUEST:
            *reply += ArduinoSendReceive(ArduinoRequest);
            break;
       case INVALID_REQUEST:
       default:
            switch(servertype)
            {
                case SVR_HTTP_LIB:
                    //httpClient.stop();
                    complete=true;
                    busy = false;
                    //httpClient.flush();
                     break;
                case SVR_COAP:            
                case SVR_MQTT:            
                case SVR_HTTP_SDK:
                default:
                    break;
            }
            *reply  += "Invalid Request\n";
            break;
   }
   //return s ;
}


/********************************************************
 * Connect to MQTT broker - then subscribe to server topic
 * Function: MqttServer_reconnect(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void MqttServer_reconnect(void) {
    int fst=10;
    bool connected = false;
    // Loop until we're reconnected (give up after 10 tries)
    while (!client->connected()&&(fst!=0)) {
        Serial.print("Attempting MQTT connection...");
        // Connect to MQTT Server
        if(mqtt_pw_enable) {
            connected = client->connect(mqtt_ci.c_str(),mqtt_un.c_str(),mqtt_pw.c_str()); 
        }
        else {
            connected = client->connect(mqtt_ci.c_str()); 
        }
        if (connected) {
            Serial.println("");
            // Successful connection message & subscribe
            Serial.println("connected");
            client->subscribe((char *)mqtt_rt.c_str());
            fst--;
       } else {
            // Failed to connect message
            Serial.print("failed, rc=");
            Serial.print(client->state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}
/********************************************************
 * MQTT server topic callback
 * Function: MqttServer_callback(char* topic, 
 *                               byte* payload, 
 *                               unsigned int length)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * topic        topic string
 * payload      value received
 * length       value length (bytes)
 * return       no return value
 ********************************************************/
void MqttServer_callback(char* topic, byte* payload, unsigned int length)
{
    String payld = "";
    char* pReply = NULL;
    RQST_Param rparam;
    pReply = (char *)os_zalloc(512);
        // Extract payload
    for (int i = 0; i < length; i++) {
        payld = payld + String((char)payload[i]);
    }
    //Msg rx message
    if(!arduino_server) {
        Serial.println(F("-------------------------------------"));
        Serial.print(F("MQTT Message Rx: topic["));
        Serial.print(topic);
        Serial.print("] \r\nPayload:");
        Serial.println(payld);
        Serial.println(F("-------------------------------------"));
    }
    // Ignore if not Server Request
    if(String(topic) != mqtt_rt) {
        os_free(pReply);
        return;
    }
    payld += " HTTP\nConnection:";

    Server_ProcessRequest(SVR_MQTT, (char*)payld.c_str(),(char*)pReply);
    os_free(pReply);
    pReply = NULL;
    payld = "";
}

/********************************************************
 * MQTT loop() service
 * Function: MqttServer_Processor(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void MqttServer_Processor(void) {
    if(wifi==1) {
        yield();
        if (!client->connected()) {
            MqttServer_reconnect();
        }
        client->loop();
        yield();
    }
}

/********************************************************
 * Initialize MQTT Broker Parameters
 * Function: MqttServer_Init(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void MqttServer_Init(void) {
    if(wifi==1) {
        // Start Mqtt server & set callback 
        client->setServer((char *)mqtt_svr.c_str(), mqtt_port);
        client->setCallback(MqttServer_callback);
        Serial.print("MQTT Rx Topic: ");
        delay(10);
        Serial.println(mqtt_rt);
        delay(10);
        Serial.print("MQTT Tx Topic: ");
        delay(10);
        Serial.println(mqtt_tt);
        delay(10);
        Serial.println("ESP8266 Mode: MQTT Client Running");
        delay(10);
    }
}

/********************************************************
 * Send request to Arduino & wait for reply 
 * Function: ArduinoSendReceive(String req)
 * 
 * Parameter      Description
 * ---------      ---------------------------------------
 * req            request string to Arduino
 * return         reply string
 ********************************************************/
String ArduinoSendReceive(String req) {
    String fromArduino = "";
    Serial.println(req);
    long start = millis();
    fromArduino = "";
    return fromArduino;
}

void MonitorSerialLine() {
    if(arduino_server) {
        while (Serial.available()) {
            // get the new byte:
            char inChar = (char)Serial.read();
            // add it to the inputString:
            SerialInString += inChar;
            // if the incoming character is a newline, set a flag
            // so the following code can process it
            if (inChar == '\n') {
                NewSerialLineRx = true;
            }
        }
        //Send Received String as MQTT Message
        if(NewSerialLineRx) {
            //If not server request, just forward Arduino Message to MQTT
            if(active_svr_rqst == SVR_NONE) {
                active_svr_rqst = SVR_MQTT;  
            }
            Server_SendReply(active_svr_rqst, REPLY_TEXT, SerialInString);
            active_svr_rqst = SVR_NONE;
            SerialInString="";
            NewSerialLineRx=false;
        }
        //Check for timeout for Arduino Server requests
         if(active_svr_rqst != SVR_NONE) {
             if(start_wait==0) start_wait = millis();
             if( (millis() - start_wait)  >5000 ) { //5 sec timeout
                  SerialInString = "no arduino reply received";
                  Server_SendReply(active_svr_rqst, REPLY_TEXT, SerialInString);
                  //Reset Request Parameters
                  active_svr_rqst = SVR_NONE;
                  start_wait=0;
                  SerialInString="";
                  NewSerialLineRx=false;
             }
         }
    }
}

/********************************************************
 * Initialize local Time 
 * Function: init_LocalTime()
 * 
 * Parameter      Description
 * ---------      ---------------------------------------
 * return         none
 ********************************************************/
void init_GmtTime() {
    if( (WiFi.status() == WL_CONNECTED) && (!gmt) ) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      Serial.print("\nWaiting for time");
      while (!time(nullptr)) {
          Serial.print(".");
          delay(1000);
      }
      Serial.print("\r\nTime has been acquired from internet time service\r\nCurrent GMT: ");
  
      time_t now = time(nullptr);
      Serial.println(ctime(&now));
      gmt = true;
    }
}
/********************************************************
 * Initialize Over the Wifi Firmware Updates 
 * Function: init_FOTA()
 * 
 * Parameter      Description
 * ---------      ---------------------------------------
 * return         none
 ********************************************************/
void init_FOTA() {
    String buff;
    int pt;

    //Hostname defaults to esp8266-[ChipID] (no change to default, which is unique (ChipID = last 3 MAC HEX)
    //ArduinoOTA.setHostname("myesp8266");
    
    //Set FOTA Network Port
    GetEepromVal(&buff, EEPROM_MQTT_PT, EEPROM_INT16);
    pt = atoi(buff.c_str());
    ArduinoOTA.setPort(pt);
     
    //Set OTA authentication (password)
    GetEepromVal(&buff, EEPROM_FOTA_PW, EEPROM_CHR);
    ArduinoOTA.setPassword((char *)buff.c_str());
    
    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.print("FOTA Initialized using IP address: ");
    Serial.println(WiFi.localIP());
}

/********************************************************
 * Sketch setup() function: Executes 1 time
 * Function: setup(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void setup() {
  
    //Initialize ESP8266 Reset Timer
    os_timer_disarm(&ResetEspTimer);
    os_timer_setfn(&ResetEspTimer, (os_timer_func_t *)ResetEspTimerCallback, NULL);
   
    EEPROM.begin(512);
    #if EEPROM_INIT==1
    InitEepromValues();
    #endif
    SetSysCfgFromEeprom();               // Set Session Cfg from EEPROM Values

    Serial.begin(serial_baud);           // Initialize serial port
    util_startWIFI();                    // Connect to local Wifi
   
    #if SVR_TYPE==SVR_HTTP_SDK
    if(!(mqtt_ssl_enable && mqtt_cl_enable)) {
        SdkWebServer_Init(http_port);    // Start SDK based web server
        init_FOTA();                     // Start FOTA Service
    }
    #endif

    if(mqtt_cl_enable) {
        client = mqtt_ssl_enable ? new PubSubClient(sslClient) : new PubSubClient(espClient);
        MqttServer_Init();              // Start MQTT Server
    }

    init_GmtTime();                      // Get GMT Time  

    pinMode(LED_IND , OUTPUT);           // Set Indicator LED as output
    digitalWrite(LED_IND, 0);            // Turn LED off (Setup complete indicator)
}


/********************************************************
 * Sketch loop() function: Executes repeatedly
 * Function: loop(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void loop() {

    util_startWIFI();                    // Connect wifi if connection dropped

    if(mqtt_cl_enable) {
        MqttServer_Processor();          // Service MQTT
    }
   
    ReadSensors(2500);                   // Read 1 sensor every 2.5 seconds or longer

    MonitorSerialLine();                 // Service Arduino Messages

    ArduinoOTA.handle();                 // Service FOTA Requests
}

