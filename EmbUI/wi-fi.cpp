// This framework originaly based on JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov
// then re-written and named by (c) 2020 Anton Zolotarev (obliterator) (https://github.com/anton-zolotarev)
// also many thanks to Vortigont (https://github.com/vortigont), kDn (https://github.com/DmytroKorniienko)
// and others people

#include "EmbUI.h"
#include "wi-fi.h"

#ifdef ESP32
union MacID
{
    uint64_t u64;
    uint8_t mc[8];
};
#endif


#ifdef ESP8266
#include "user_interface.h"

void EmbUI::onSTAConnected(WiFiEventStationModeConnected ipInfo)
{
    LOG(printf_P, PSTR("UI WiFi: STA connected - SSID:'%s'"), ipInfo.ssid.c_str());
    if(_cb_STAConnected)
        _cb_STAConnected();        // execule callback
}

void EmbUI::onSTAGotIP(WiFiEventStationModeGotIP ipInfo)
{
    sysData.wifi_sta = true;
    embuischedw.disable();
    LOG(printf_P, PSTR(", IP: %s\n"), ipInfo.ip.toString().c_str());
    timeProcessor.onSTAGotIP(ipInfo);
    if(_cb_STAGotIP)
        _cb_STAGotIP();        // execule callback

    setup_mDns();

    // shutdown AP after timeout
    embuischedw.disable();
    embuischedw.set(WIFI_CONNECT_TIMEOUT * TASK_SECOND, TASK_ONCE, [](){
        if(WiFi.getMode() == WIFI_STA)
            return;

        WiFi.enableAP(false);
        LOG(println, F("UI WiFi: AP mode disabled"));
    });
    embuischedw.restartDelayed();
}

void EmbUI::onSTADisconnected(WiFiEventStationModeDisconnected event_info)
{
    LOG(printf_P, PSTR("UI WiFi: Disconnected from SSID: %s, reason: %d\n"), event_info.ssid.c_str(), event_info.reason);
    sysData.wifi_sta = false;       // to be removed and replaced with API-method

    if (embuischedw.isEnabled())
        return;

    if(param(FPSTR(P_APonly)) == "1")
        return;

    /*
      esp8266 сильно тормозит в комбинированном режиме AP-STA при постоянных попытках реконнекта, WEB-интерфейс становится
      неотзывчивым, сложно изменять настройки.
      В качестве решения переключаем контроллер в режим AP-only после WIFI_CONNECT_TIMEOUT таймаута на попытку переподключения.
      Далее делаем периодические попытки переподключений каждые WIFI_RECONNECT_TIMER секунд
    */
    embuischedw.set(WIFI_CONNECT_TIMEOUT * TASK_SECOND, TASK_ONCE, [this](){
        wifi_setAP();
        WiFi.enableSTA(false);
        Task *t = new Task(WIFI_RECONNECT_TIMER * TASK_SECOND, TASK_ONCE,
                [this](){ embuischedw.disable();
                WiFi.enableSTA(true);
                WiFi.begin();
                TASK_RECYCLE; },
                &ts, false
            );
        t->enableDelayed();
    } );

    embuischedw.restartDelayed();

    timeProcessor.onSTADisconnected(event_info);
    if(_cb_STADisconnected)
        _cb_STADisconnected();        // execute callback
}

void EmbUI::onWiFiMode(WiFiEventModeChange event_info){
/*
    if(WiFi.getMode() == WIFI_AP){
        setup_mDns();
    }
*/
}
#endif  //ESP8266

#ifdef ESP32
void EmbUI::WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event){
/*
    case SYSTEM_EVENT_AP_START:
        LOG(println, F("UI WiFi: Access-point started"));
        setup_mDns();
        break;
*/
    case SYSTEM_EVENT_STA_CONNECTED:
        LOG(println, F("UI WiFi: STA connected"));

        if(_cb_STAConnected)
            _cb_STAConnected();        // execule callback
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
    	/* this is a weird hack to mitigate DHCP-client hostname issue
	     * https://github.com/espressif/arduino-esp32/issues/2537
         * we use some IDF functions to restart dhcp-client, that has been disabled before STA connect
	    */
	    tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
	    tcpip_adapter_ip_info_t iface;
	    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &iface);
        if(!iface.ip.addr){
            LOG(print, F("UI WiFi: DHCP discover... "));
	        return;
    	}

        LOG(printf_P, PSTR("SSID:'%s', IP: "), WiFi.SSID().c_str());  // IPAddress(info.got_ip.ip_info.ip.addr)
        LOG(println, IPAddress(iface.ip.addr));

        embuischedw.disable();
        embuischedw.set(WIFI_CONNECT_TIMEOUT * TASK_SECOND, TASK_ONCE, [](){
            if(WiFi.getMode() == WIFI_MODE_STA)
                return;

            WiFi.enableAP(false);
            LOG(println, F("UI WiFi: AP mode disabled"));
        });
        embuischedw.restartDelayed();

        sysData.wifi_sta = true;
        setup_mDns();
        if(_cb_STAGotIP)
            _cb_STAGotIP();        // execule callback
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        #ifdef ARDUINO  // stuped arduino core does not have defines for it's own version, so I use PIO's platform defs here
            #if ARDUINO <= 10805
                LOG(printf_P, PSTR("UI WiFi: Disconnected, reason: %d\n"), info.disconnected.reason);           // ARDUINO=10805    Core <=1.0.6
            #else
                LOG(printf_P, PSTR("UI WiFi: Disconnected, reason: %d\n"), info.wifi_sta_disconnected.reason);  // ARDUINO=10812    Core >=2.0.0
            #endif
        #endif
        
        // https://github.com/espressif/arduino-esp32/blob/master/tools/sdk/include/esp32/esp_wifi_types.h
        if(WiFi.getMode() != WIFI_MODE_APSTA && !embuischedw.isEnabled()){
            LOG(println, PSTR("UI WiFi: Reconnect attempt"));
            embuischedw.set(WIFI_CONNECT_TIMEOUT * TASK_SECOND, TASK_ONCE, [this](){
                        LOG(println, F("UI WiFi: Switch to AP/STA mode"));
                        WiFi.enableAP(true);
                        WiFi.begin();
                        } );
            embuischedw.restartDelayed();
        }

        sysData.wifi_sta = false;
        if(_cb_STADisconnected)
            _cb_STADisconnected();        // execule callback
        break;

    default:
        break;
    }
    timeProcessor.WiFiEvent(event, info);    // handle network events for timelib
}
#endif  //ESP32

void EmbUI::wifi_init(){

    String apmode = param(FPSTR(P_APonly));

    LOG(print, F("UI WiFi: start in "));
    if (apmode == FPSTR(P_true)){
        LOG(println, F("AP-only mode"));
        wifi_setAP(true);
        return;
    }

    LOG(println, F("STA mode"));
    WiFi.enableSTA(true);

    #ifdef ESP8266
        WiFi.hostname(hostname());
        WiFi.begin();   // use internaly stored last known credentials for connection
    #elif defined ESP32
    	/* this is a weird hack to mitigate DHCP hostname issue
	     * order of initialization does matter, pls keep it like this till fixed in upstream
	     * https://github.com/espressif/arduino-esp32/issues/2537
	     */
	    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        // use internaly stored last known credentials for connection
        if ( WiFi.begin() == WL_CONNECT_FAILED ){
            embuischedw.set(WIFI_BEGIN_DELAY * TASK_SECOND, TASK_ONCE, [this](){
                            wifi_setAP();
                            WiFi.enableSTA(true);
                            LOG(println, F("UI WiFi: Switch to AP/STA mode"));}
            );
            embuischedw.restartDelayed();
        }

	    if (!WiFi.setHostname(hostname()))
            LOG(println, F("UI WiFi: Failed to set hostname :("));
    #endif
    LOG(println, F("UI WiFi: STA reconecting..."));
}

void EmbUI::wifi_connect(const char *ssid, const char *pwd)
{
    String _ssid(ssid); String _pwd(pwd);   // I need objects to pass it to lambda
    embuischedw.set(WIFI_BEGIN_DELAY * TASK_SECOND, TASK_ONCE,
        [_ssid, _pwd](){
                LOG(printf_P, PSTR("UI WiFi: client connecting to SSID:%s, pwd:%s\n"), _ssid.c_str(), _pwd.c_str());
                #ifdef ESP32
                    WiFi.disconnect();
                    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
                #endif
                _ssid.length() ? WiFi.begin(_ssid.c_str(), _pwd.c_str()) : WiFi.begin();
                ts.getCurrentTask()->disable();
        }
    );

    embuischedw.restartDelayed();
}


void EmbUI::wifi_setmode(WiFiMode_t mode){
    LOG(printf_P, PSTR("UI WiFi: set mode: %d\n"), mode);
    WiFi.mode(mode);
}

/*use mdns for host name resolution*/
void EmbUI::setup_mDns(){

#ifdef ESP8266
    if (MDNS.isRunning())
#endif
        MDNS.end();

    if (!MDNS.begin(hostname())){
        LOG(println, F("UI mDNS: Error setting up responder!"));
        MDNS.end();
        return;
    }

    MDNS.addService(F("http"), F("tcp"), 80);
    //MDNS.addService(F("ftp"), F("tcp"), 21);
    MDNS.addService(F("txt"), F("udp"), 4243);
    LOG(printf_P, PSTR("UI mDNS: responder started: %s.local\n"), hostname());
}


/**
 * формирует chipid из MAC-адреса вида 'ddeeff'
 */
void EmbUI::getmacid(){
#ifdef ESP32
    MacID _mac;
    _mac.u64 = ESP.getEfuseMac();

    //LOG(printf_P,PSTR("UI MAC ID:%06llX\n"), _mac.id);
    // EfuseMac LSB comes first, so need to transpose bytes
    sprintf_P(mc, PSTR("%02X%02X%02X"), _mac.mc[3], _mac.mc[4], _mac.mc[5]);
    LOG(printf_P,PSTR("UI ID:%02X%02X%02X\n"), _mac.mc[3], _mac.mc[4], _mac.mc[5]);
#endif

#ifdef ESP8266
    uint8_t _mac[6];
    WiFi.softAPmacAddress(_mac);

    sprintf_P(mc, PSTR("%02X%02X%02X"), _mac[3], _mac[4], _mac[5]);
    LOG(printf_P,PSTR("UI ID:%02X%02X%02X\n"), _mac[3], _mac[4], _mac[5]);
#endif
}

/**
 * Configure esp's internal AP
 * default is to configure credentials from the config
 * bool force - reapply credentials even if AP is already started, exit otherwise
 */
void EmbUI::wifi_setAP(bool force){
    // check if AP is already started
    if ((bool)(WiFi.getMode() & WIFI_AP) & !force)
        return;

    // clear password if invalid 
    if (cfg[FPSTR(P_APpwd)] && strlen(cfg[FPSTR(P_APpwd)]) < WIFI_PSK_MIN_LENGTH)
        var_remove(FPSTR(P_APpwd));

    LOG(printf_P, PSTR("UI WiFi: set AP params to SSID:%s, pwd:%s\n"), hostname(), cfg[FPSTR(P_APpwd)] ? cfg[FPSTR(P_APpwd)].as<const char*>() : "");

    WiFi.softAP(hostname(), cfg[FPSTR(P_APpwd)]);

    // run mDNS in WiFi-AP mode
    setup_mDns();
}


void EmbUI::wifi_updateAP() {
    wifi_setAP(true);

    if (paramVariant(FPSTR(P_APonly))){
        LOG(println, F("UI WiFi: Force AP mode"));
        WiFi.enableAP(true);
        WiFi.enableSTA(false);
    }
}

/**
 * @brief get/set device hosname
 * if hostname has not been set or empty returns autogenerated __IDPREFIX-[mac_id] hostname
 * autogenerated hostname is NOT saved into persistent config
 * 
 * @return const char* current hostname
 */
const char* EmbUI::hostname(){

    JsonVariantConst h = paramVariant(FPSTR(P_hostname));
    if (h && strlen(h.as<const char*>()))
        return h.as<const char*>();

    if (autohostname.get())
        return autohostname.get();

    LOG(println, F("generate autohostname"));

    autohostname = std::unique_ptr<char[]>(new char[sizeof(__IDPREFIX) + sizeof(mc) + 2]);
    sprintf_P(autohostname.get(), PSTR(__IDPREFIX "-%s"), mc);
    return autohostname.get();
}

const char* EmbUI::hostname(const char* name){
    var_dropnulls(FPSTR(P_hostname), (char*)name);
    return hostname();
};
