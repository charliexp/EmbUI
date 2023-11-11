/*
    This file is part of EmbUI project
    https://github.com/vortigont/EmbUI

    Copyright © 2023 Emil Muratov (Vortigont)   https://github.com/vortigont/

    EmbUI is free software: you can redistribute it and/or modify
    it under the terms of MIT License https://opensource.org/license/mit/

    This framework originaly based on JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov
    then re-written and named by (c) 2020 Anton Zolotarev (obliterator) (https://github.com/anton-zolotarev)
    also many thanks to Vortigont (https://github.com/vortigont), kDn (https://github.com/DmytroKorniienko)
    and others people
*/

#include "mqtt.h"

#define MQTT_RECONNECT_PERIOD    15

void EmbUI::_mqttConnTask(bool state){
    if (!state){
        tMqttReconnector->disable();
        //delete tMqttReconnector;
        //tMqttReconnector = nullptr;
        return;
    }

    if(tMqttReconnector){
        tValPublisher->restart();
    } else {
        tMqttReconnector = new Task(MQTT_RECONNECT_PERIOD * TASK_SECOND, TASK_FOREVER, [this](){
            if (!(WiFi.getMode() & WIFI_MODE_STA)) return;
            if (mqttClient) _connectToMqtt();
        }, &ts, true, nullptr, [this](){ tMqttReconnector = nullptr; }, true );
    }
}

void EmbUI::_connectToMqtt() {
    LOG(println, PSTR("Connecting to MQTT..."));

    if (cfg[V_mqtt_topic]){
        mqtt_topic = cfg[V_mqtt_topic].as<const char*>();
        mqtt_topic.replace("$id", mc);
    } else {
        mqtt_topic = "EmbUI/";
        mqtt_topic += mc;
        mqtt_topic += (char)0x2f; // "/"
    }

    mqtt_host = paramVariant(V_mqtt_host).as<const char*>();
    mqtt_port = cfg[V_mqtt_port];
    mqtt_user = paramVariant(V_mqtt_user).as<const char*>();
    mqtt_pass = paramVariant(V_mqtt_pass).as<const char*>();
    //mqtt_lwt=id("embui/pub/online");

    //if (mqttClient->connected())
        mqttClient->disconnect(true);

    IPAddress ip; 

    if(ip.fromString(mqtt_host))
        mqttClient->setServer(ip, mqtt_port);
    else
        mqttClient->setServer(mqtt_host.c_str(), mqtt_port);

    mqttClient->setKeepAlive(mqtt_ka);
    mqttClient->setCredentials(mqtt_user.c_str(), mqtt_pass.c_str());
    //setWill(mqtt_lwt.c_str(), 0, true, "0").
    mqttClient->connect();
}

void EmbUI::mqttStart(){
    if (cfg[V_mqtt_enable] != true || !cfg.containsKey(V_mqtt_host)){
        LOG(println, "UI: MQTT disabled or no host set");
        return;   // выходим если host не задан
    }

    LOG(println, "Starting MQTT Client");
    if (!mqttClient)
        mqttClient = std::make_unique<AsyncMqttClient>();

    mqttClient->onConnect([this](bool sessionPresent){ _onMqttConnect(sessionPresent); });
    mqttClient->onDisconnect([this](AsyncMqttClientDisconnectReason reason){_onMqttDisconnect(reason);});
    mqttClient->onMessage( [this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total){_onMqttMessage(topic, payload, properties, len, index, total);} );
    //mqttClient->onSubscribe(onMqttSubscribe);
    //mqttClient->onUnsubscribe(onMqttUnsubscribe);
    //mqttClient->onPublish(onMqttPublish);

    mqttReconnect();
}

void EmbUI::mqttStop(){
    _mqttConnTask(false);
    delete mqttClient.release();
}

void EmbUI::mqttReconnect(){ // принудительный реконнект, при смене чего-либо в UI
    _mqttConnTask(true);
}

void EmbUI::_onMqttDisconnect(AsyncMqttClientDisconnectReason reason){
  LOG(printf, "UI: Disconnected from MQTT:%u\n", static_cast<uint8_t>(reason));
  feeders.remove(_mqtt_feed_id);    // remove MQTT feeder from chain
  //mqttReconnect();
}

void EmbUI::_onMqttConnect(bool sessionPresent){
    LOG(println,"UI: Connected to MQTT.");
    _mqttConnTask(false);
    _mqttSubscribe();
    // mqttClient->publish(mqtt_lwt.c_str(), 0, true, "1");  // publish Last Will testament

    // create MQTT feeder and add into the chain
    _mqtt_feed_id = feeders.add( std::make_unique<FrameSendMQTT>(this) );

    // publish sys info
    String t(C_sys);
    publish((t + V_hostname).c_str(), hostname(), true);
    publish((t + "ip").c_str(), WiFi.localIP().toString().c_str(), true);
    publish((t + P_uiver).c_str(), EMBUI_VERSION_STRING, true);
    publish((t + P_uijsapi).c_str(), EMBUI_JSAPI, true);
}

void EmbUI::_onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    LOG(printf, "UI: Got MQTT msg topic: %s len:%u/%u\n", topic, len, total);
    if (index || len != total) return;     // this is chunked message, reassembly is not supported (yet)

    std::string_view tpc(topic);
/*
    const auto pos = tpc.find(mqttPrefix().c_str());
    if (pos == tpc.npos)
        tpc.remove_prefix(mqttPrefix().length());
*/

    // this is a dublicate code same as for WS, need to implement a proper queue for such data

    uint16_t objCnt = 0;
    for(uint16_t i=0; i<len; ++i)
        if(payload[i]==0x3a || payload[i]==0x7b)    // считаем ':' и '{' это учитывает и пары k:v и вложенные массивы
            ++objCnt;

    DynamicJsonDocument *res = new DynamicJsonDocument(len + JSON_OBJECT_SIZE(objCnt) + 64); // https://arduinojson.org/v6/assistant/
    if(!res->capacity())
        return;

    DeserializationError error = deserializeJson((*res), (const char*)payload, len); // deserialize via copy to prevent dangling pointers in action()'s
    if (error){
        LOG(printf, "MQTT: msg deserialization err: %d\n", error.code());
        delete res;
        return;
    }

    tpc.remove_prefix(mqttPrefix().length());     // chop off constant prefix

    if (starts_with(tpc, C_get) || starts_with(tpc, C_set)){
        std::string act(tpc.substr(4));                     // chop off 'get/' or 'set/' prefix
        std::replace( act.begin(), act.end(), '/', '_');    // replace topic delimiters into underscores
        JsonObject o = res->as<JsonObject>();
        o[P_action] = act;                                  // set action identifier
    }

    // switch context for processing data
    Task *t = new Task(10, TASK_ONCE,
        [res](){
            JsonObject o = res->as<JsonObject>();
            // call action handler for post'ed data
            embui.post(o);
            delete res; },
        &ts, false, nullptr, nullptr, true
    );
    if (t)
        t->enableDelayed();


}

void EmbUI::_mqttSubscribe(){
    mqttClient->subscribe((mqttPrefix()+"set/#").c_str(), 0);
    mqttClient->subscribe((mqttPrefix()+"get/#").c_str(), 0);
    mqttClient->subscribe((mqttPrefix()+C_post).c_str(), 0);
    //    t += (char)0x23;  //"#"
}

void EmbUI::publish(const char* topic, const char* payload, bool retained){
    if (!mqttAvailable()) return;
    std::string t(mqttPrefix().c_str());
    t += topic;    // make topic string "~/{$topic}/"
    /*
    LOG(print, "MQTT pub: topic:");
    LOG(print, topic);
    LOG(print, " payload:");
    LOG(println, payload);
    */
    mqttClient->publish(t.data(), 0, retained, payload);
}

void EmbUI::publish(const char* topic, const JsonVariantConst& data, bool retained){
    if (!mqttAvailable()) return;
    auto s = measureJson(data);
    std::vector<uint8_t> buff(s);
    serializeJson(data, static_cast<unsigned char*>(buff.data()), s);
    std::string t(mqttPrefix().c_str());
    t += topic;
    mqttClient->publish(t.data(), 0, retained, reinterpret_cast<const char*>(buff.data()), buff.size());
}

void EmbUI::_mqtt_pub_sys_status(){
    String t(C_sys);
    if(psramFound())
        publish((t + "spiram_free").c_str(), ESP.getFreePsram()/1024);

    publish((t + "heap_free").c_str(), ESP.getFreeHeap()/1024);
    publish((t + "uptime").c_str(), esp_timer_get_time() / 1000000);
    publish((t + "rssi").c_str(), WiFi.RSSI());
}

void FrameSendMQTT::send(const JsonVariantConst& data){
    if (data[P_pkg] == P_value){
        _eu->publish(C_pub_value, data[P_block]);
        return;
    }

    // objects like "interface", "xload", "section" are related to WebUI interface
    if (data[P_pkg] == P_interface || data[P_pkg] == P_xload || data.containsKey(P_section) ){
        _eu->publish(C_pub_iface, data);
        return;
    }

    _eu->publish(C_pub_etc, data);
}