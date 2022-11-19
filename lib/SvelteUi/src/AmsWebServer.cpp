#include "AmsWebServer.h"
#include "AmsWebHeaders.h"
#include "base64.h"
#include "hexutils.h"

#include <ArduinoJson.h>

#include "html/index_html.h"
#include "html/index_css.h"
#include "html/index_js.h"
#include "html/github_svg.h"
#include "html/data_json.h"
#include "html/dayplot_json.h"
#include "html/monthplot_json.h"
#include "html/energyprice_json.h"
#include "html/tempsensor_json.h"

#include "version.h"


AmsWebServer::AmsWebServer(uint8_t* buf, RemoteDebug* Debug, HwTools* hw) {
	this->debugger = Debug;
	this->hw = hw;
	this->buf = (char*) buf;
}

void AmsWebServer::setup(AmsConfiguration* config, GpioConfig* gpioConfig, MeterConfig* meterConfig, AmsData* meterState, AmsDataStorage* ds, EnergyAccounting* ea) {
    this->config = config;
	this->gpioConfig = gpioConfig;
	this->meterConfig = meterConfig;
	this->meterState = meterState;
	this->ds = ds;
	this->ea = ea;

	// TODO
	server.on(F("/"), HTTP_GET, std::bind(&AmsWebServer::indexHtml, this));
	server.on(F("/configuration"), HTTP_GET, std::bind(&AmsWebServer::indexHtml, this));
	server.on(F("/status"), HTTP_GET, std::bind(&AmsWebServer::indexHtml, this));
	server.on(F("/index.css"), HTTP_GET, std::bind(&AmsWebServer::indexCss, this));
	server.on(F("/index.js"), HTTP_GET, std::bind(&AmsWebServer::indexJs, this));
	server.on(F("/github.svg"), HTTP_GET, std::bind(&AmsWebServer::githubSvg, this)); 
	server.on(F("/sysinfo.json"), HTTP_GET, std::bind(&AmsWebServer::sysinfoJson, this));
	server.on(F("/data.json"), HTTP_GET, std::bind(&AmsWebServer::dataJson, this));
	server.on(F("/dayplot.json"), HTTP_GET, std::bind(&AmsWebServer::dayplotJson, this));
	server.on(F("/monthplot.json"), HTTP_GET, std::bind(&AmsWebServer::monthplotJson, this));
	server.on(F("/energyprice.json"), HTTP_GET, std::bind(&AmsWebServer::energyPriceJson, this));
	server.on(F("/temperature.json"), HTTP_GET, std::bind(&AmsWebServer::temperatureJson, this));

	server.on(F("/wifiscan.json"), HTTP_GET, std::bind(&AmsWebServer::wifiScanJson, this));

	server.on(F("/configuration.json"), HTTP_GET, std::bind(&AmsWebServer::configurationJson, this));
	server.on(F("/save"), HTTP_POST, std::bind(&AmsWebServer::handleSave, this));
	server.on(F("/reboot"), HTTP_POST, std::bind(&AmsWebServer::reboot, this));
		
	server.onNotFound(std::bind(&AmsWebServer::notFound, this));
	
	server.begin(); // Web server start

	config->getWebConfig(webConfig);
	MqttConfig mqttConfig;
	config->getMqttConfig(mqttConfig);
	mqttEnabled = strlen(mqttConfig.host) > 0;
}


void AmsWebServer::setMqtt(MQTTClient* mqtt) {
	this->mqtt = mqtt;
}

void AmsWebServer::setTimezone(Timezone* tz) {
	this->tz = tz;
}

void AmsWebServer::setMqttEnabled(bool enabled) {
	mqttEnabled = enabled;
}

void AmsWebServer::setEntsoeApi(EntsoeApi* eapi) {
	this->eapi = eapi;
}

void AmsWebServer::loop() {
	server.handleClient();

	if(maxPwr == 0 && meterState->getListType() > 1 && meterConfig->mainFuse > 0 && meterConfig->distributionSystem > 0) {
		int voltage = meterConfig->distributionSystem == 2 ? 400 : 230;
		if(meterState->isThreePhase()) {
			maxPwr = meterConfig->mainFuse * sqrt(3) * voltage;
		} else if(meterState->isTwoPhase()) {
			maxPwr = meterConfig->mainFuse * voltage;
		} else {
			maxPwr = meterConfig->mainFuse * 230;
		}
	}
}

bool AmsWebServer::checkSecurity(byte level) {
	bool access = WiFi.getMode() == WIFI_AP || webConfig.security < level;
	if(!access && webConfig.security >= level && server.hasHeader("Authorization")) {
		String expectedAuth = String(webConfig.username) + ":" + String(webConfig.password);

		String providedPwd = server.header("Authorization");
		providedPwd.replace("Basic ", "");

		#if defined(ESP8266)
		String expectedBase64 = base64::encode(expectedAuth, false);
		#elif defined(ESP32)
		String expectedBase64 = base64::encode(expectedAuth);
		#endif

		debugger->printf("Expected auth: %s\n", expectedBase64.c_str());
		debugger->printf("Provided auth: %s\n", providedPwd.c_str());

		access = providedPwd.equals(expectedBase64);
	}

	if(!access) {
		server.sendHeader(HEADER_AUTHENTICATE, AUTHENTICATE_BASIC);
		server.setContentLength(0);
		server.send(401, MIME_HTML, "");
	}
	return access;
}

void AmsWebServer::notFound() {
	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_1HR);
		server.send(404);
}
void AmsWebServer::githubSvg() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /github.svg over http...\n");

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_1HR);
	server.send_P(200, "image/svg+xml", GITHUB_SVG);
}

void AmsWebServer::sysinfoJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /sysinfo.json over http...\n");

	DynamicJsonDocument doc(512);
	doc["version"] = VERSION;
	#if defined(CONFIG_IDF_TARGET_ESP32S2)
	doc["chip"] = "esp32s2";
	#elif defined(CONFIG_IDF_TARGET_ESP32C3)
	doc["chip"] = "esp32c3";
	#elif defined(ESP32)
	doc["chip"] = "esp32";
	#elif defined(ESP8266)
	doc["chip"] = "esp8266";
	#endif

	uint16_t chipId;
	#if defined(ESP32)
		chipId = ESP.getEfuseMac();
	#else
		chipId = ESP.getChipId();
	#endif
	doc["chipId"] = String(chipId, HEX);

	SystemConfig sys;
	config->getSystemConfig(sys);
	doc["board"] = sys.boardType;
	doc["vndcfg"] = sys.vendorConfigured;
	doc["usrcfg"] = sys.userConfigured;
	doc["fwconsent"] = sys.dataCollectionConsent;
	doc["country"] = sys.country;

	doc["net"]["ip"] = WiFi.localIP().toString();
	doc["net"]["mask"] = WiFi.subnetMask().toString();
	doc["net"]["gw"] = WiFi.gatewayIP().toString();
	doc["net"]["dns1"] = WiFi.dnsIP(0).toString();
	doc["net"]["dns2"] = WiFi.dnsIP(1).toString();

	doc["meter"]["mfg"] = meterState->getMeterType();
	doc["meter"]["model"] = meterState->getMeterModel();
	doc["meter"]["id"] = meterState->getMeterId();

	serializeJson(doc, buf, BufferSize);
	server.send(200, MIME_JSON, buf);
}

void AmsWebServer::dataJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /data.json over http...\n");
	uint64_t now = millis64();

	if(!checkSecurity(2))
		return;

	float vcc = hw->getVcc();
	int rssi = hw->getWifiRssi();

	uint8_t espStatus;
	#if defined(ESP8266)
	if(vcc == 0) {
		espStatus = 1;
	} else if(vcc > 3.1 && vcc < 3.5) {
		espStatus = 1;
	} else if(vcc > 3.0 && vcc < 3.6) {
		espStatus = 2;
	} else {
		espStatus = 3;
	}
	#elif defined(ESP32)
	if(vcc == 0) {
		espStatus = 1;
	} else if(vcc > 2.8 && vcc < 3.5) {
		espStatus = 1;
	} else if(vcc > 2.7 && vcc < 3.6) {
		espStatus = 2;
	} else {
		espStatus = 3;
	}
	#endif


	uint8_t hanStatus;
	if(meterConfig->baud == 0) {
		hanStatus = 0;
	} else if(now - meterState->getLastUpdateMillis() < 15000) {
		hanStatus = 1;
	} else if(now - meterState->getLastUpdateMillis() < 30000) {
		hanStatus = 2;
	} else {
		hanStatus = 3;
	}

	uint8_t wifiStatus;
	if(rssi > -75) {
		wifiStatus = 1;
	} else if(rssi > -95) {
		wifiStatus = 2;
	} else {
		wifiStatus = 3;
	}

	uint8_t mqttStatus;
	if(!mqttEnabled) {
		mqttStatus = 0;
	} else if(mqtt != NULL && mqtt->connected()) {
		mqttStatus = 1;
	} else if(mqtt != NULL && mqtt->lastError() == 0) {
		mqttStatus = 2;
	} else {
		mqttStatus = 3;
	}

	float price = ENTSOE_NO_VALUE;
	if(eapi != NULL && strlen(eapi->getToken()) > 0)
		price = eapi->getValueForHour(0);

	String peaks = "";
	for(uint8_t i = 1; i <= ea->getConfig()->hours; i++) {
		if(!peaks.isEmpty()) peaks += ",";
		peaks += String(ea->getPeak(i));
	}

	snprintf_P(buf, BufferSize, DATA_JSON,
		maxPwr == 0 ? meterState->isThreePhase() ? 20000 : 10000 : maxPwr,
		meterConfig->productionCapacity,
		meterConfig->mainFuse == 0 ? 32 : meterConfig->mainFuse,
		meterState->getActiveImportPower(),
		meterState->getActiveExportPower(),
		meterState->getReactiveImportPower(),
		meterState->getReactiveExportPower(),
		meterState->getActiveImportCounter(),
		meterState->getActiveExportCounter(),
		meterState->getReactiveImportCounter(),
		meterState->getReactiveExportCounter(),
		meterState->getL1Voltage(),
		meterState->getL2Voltage(),
		meterState->getL3Voltage(),
		meterState->getL1Current(),
		meterState->getL2Current(),
		meterState->getL3Current(),
		meterState->getPowerFactor(),
		meterState->getL1PowerFactor(),
		meterState->getL2PowerFactor(),
		meterState->getL3PowerFactor(),
		vcc,
		rssi,
		hw->getTemperature(),
		(uint32_t) (now / 1000),
		ESP.getFreeHeap(),
		espStatus,
		hanStatus,
		wifiStatus,
		mqttStatus,
		mqtt == NULL ? 0 : (int) mqtt->lastError(),
		price == ENTSOE_NO_VALUE ? "null" : String(price, 2).c_str(),
		meterState->getMeterType(),
		meterConfig->distributionSystem,
		ea->getMonthMax(),
		peaks.c_str(),
		ea->getCurrentThreshold(),
		ea->getUseThisHour(),
		ea->getCostThisHour(),
		ea->getProducedThisHour(),
		ea->getUseToday(),
		ea->getCostToday(),
		ea->getProducedToday(),
		ea->getUseThisMonth(),
		ea->getCostThisMonth(),
		ea->getProducedThisMonth(),
		(uint32_t) time(nullptr)
	);

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	server.setContentLength(strlen(buf));
	server.send(200, MIME_JSON, buf);
}

void AmsWebServer::dayplotJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /dayplot.json over http...\n");

	if(!checkSecurity(2))
		return;

	if(ds == NULL) {
		notFound();
	} else {
		snprintf_P(buf, BufferSize, DAYPLOT_JSON,
			ds->getHourImport(0) / 1000.0,
			ds->getHourImport(1) / 1000.0,
			ds->getHourImport(2) / 1000.0,
			ds->getHourImport(3) / 1000.0,
			ds->getHourImport(4) / 1000.0,
			ds->getHourImport(5) / 1000.0,
			ds->getHourImport(6) / 1000.0,
			ds->getHourImport(7) / 1000.0,
			ds->getHourImport(8) / 1000.0,
			ds->getHourImport(9) / 1000.0,
			ds->getHourImport(10) / 1000.0,
			ds->getHourImport(11) / 1000.0,
			ds->getHourImport(12) / 1000.0,
			ds->getHourImport(13) / 1000.0,
			ds->getHourImport(14) / 1000.0,
			ds->getHourImport(15) / 1000.0,
			ds->getHourImport(16) / 1000.0,
			ds->getHourImport(17) / 1000.0,
			ds->getHourImport(18) / 1000.0,
			ds->getHourImport(19) / 1000.0,
			ds->getHourImport(20) / 1000.0,
			ds->getHourImport(21) / 1000.0,
			ds->getHourImport(22) / 1000.0,
			ds->getHourImport(23) / 1000.0,
			ds->getHourExport(0) / 1000.0,
			ds->getHourExport(1) / 1000.0,
			ds->getHourExport(2) / 1000.0,
			ds->getHourExport(3) / 1000.0,
			ds->getHourExport(4) / 1000.0,
			ds->getHourExport(5) / 1000.0,
			ds->getHourExport(6) / 1000.0,
			ds->getHourExport(7) / 1000.0,
			ds->getHourExport(8) / 1000.0,
			ds->getHourExport(9) / 1000.0,
			ds->getHourExport(10) / 1000.0,
			ds->getHourExport(11) / 1000.0,
			ds->getHourExport(12) / 1000.0,
			ds->getHourExport(13) / 1000.0,
			ds->getHourExport(14) / 1000.0,
			ds->getHourExport(15) / 1000.0,
			ds->getHourExport(16) / 1000.0,
			ds->getHourExport(17) / 1000.0,
			ds->getHourExport(18) / 1000.0,
			ds->getHourExport(19) / 1000.0,
			ds->getHourExport(20) / 1000.0,
			ds->getHourExport(21) / 1000.0,
			ds->getHourExport(22) / 1000.0,
			ds->getHourExport(23) / 1000.0
		);

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

		server.setContentLength(strlen(buf));
		server.send(200, MIME_JSON, buf);
	}
}

void AmsWebServer::monthplotJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /monthplot.json over http...\n");

	if(!checkSecurity(2))
		return;

	if(ds == NULL) {
		notFound();
	} else {
		snprintf_P(buf, BufferSize, MONTHPLOT_JSON,
			ds->getDayImport(1) / 1000.0,
			ds->getDayImport(2) / 1000.0,
			ds->getDayImport(3) / 1000.0,
			ds->getDayImport(4) / 1000.0,
			ds->getDayImport(5) / 1000.0,
			ds->getDayImport(6) / 1000.0,
			ds->getDayImport(7) / 1000.0,
			ds->getDayImport(8) / 1000.0,
			ds->getDayImport(9) / 1000.0,
			ds->getDayImport(10) / 1000.0,
			ds->getDayImport(11) / 1000.0,
			ds->getDayImport(12) / 1000.0,
			ds->getDayImport(13) / 1000.0,
			ds->getDayImport(14) / 1000.0,
			ds->getDayImport(15) / 1000.0,
			ds->getDayImport(16) / 1000.0,
			ds->getDayImport(17) / 1000.0,
			ds->getDayImport(18) / 1000.0,
			ds->getDayImport(19) / 1000.0,
			ds->getDayImport(20) / 1000.0,
			ds->getDayImport(21) / 1000.0,
			ds->getDayImport(22) / 1000.0,
			ds->getDayImport(23) / 1000.0,
			ds->getDayImport(24) / 1000.0,
			ds->getDayImport(25) / 1000.0,
			ds->getDayImport(26) / 1000.0,
			ds->getDayImport(27) / 1000.0,
			ds->getDayImport(28) / 1000.0,
			ds->getDayImport(29) / 1000.0,
			ds->getDayImport(30) / 1000.0,
			ds->getDayImport(31) / 1000.0,
			ds->getDayExport(1) / 1000.0,
			ds->getDayExport(2) / 1000.0,
			ds->getDayExport(3) / 1000.0,
			ds->getDayExport(4) / 1000.0,
			ds->getDayExport(5) / 1000.0,
			ds->getDayExport(6) / 1000.0,
			ds->getDayExport(7) / 1000.0,
			ds->getDayExport(8) / 1000.0,
			ds->getDayExport(9) / 1000.0,
			ds->getDayExport(10) / 1000.0,
			ds->getDayExport(11) / 1000.0,
			ds->getDayExport(12) / 1000.0,
			ds->getDayExport(13) / 1000.0,
			ds->getDayExport(14) / 1000.0,
			ds->getDayExport(15) / 1000.0,
			ds->getDayExport(16) / 1000.0,
			ds->getDayExport(17) / 1000.0,
			ds->getDayExport(18) / 1000.0,
			ds->getDayExport(19) / 1000.0,
			ds->getDayExport(20) / 1000.0,
			ds->getDayExport(21) / 1000.0,
			ds->getDayExport(22) / 1000.0,
			ds->getDayExport(23) / 1000.0,
			ds->getDayExport(24) / 1000.0,
			ds->getDayExport(25) / 1000.0,
			ds->getDayExport(26) / 1000.0,
			ds->getDayExport(27) / 1000.0,
			ds->getDayExport(28) / 1000.0,
			ds->getDayExport(29) / 1000.0,
			ds->getDayExport(30) / 1000.0,
			ds->getDayExport(31) / 1000.0
		);

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

		server.setContentLength(strlen(buf));
		server.send(200, MIME_JSON, buf);
	}
}

void AmsWebServer::energyPriceJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /energyprice.json over http...\n");

	if(!checkSecurity(2))
		return;

	float prices[36];
	for(int i = 0; i < 36; i++) {
		prices[i] = eapi == NULL ? ENTSOE_NO_VALUE : eapi->getValueForHour(i);
	}

	snprintf_P(buf, BufferSize, ENERGYPRICE_JSON, 
		eapi == NULL ? "" : eapi->getCurrency(),
		prices[0] == ENTSOE_NO_VALUE ? "null" : String(prices[0], 4).c_str(),
		prices[1] == ENTSOE_NO_VALUE ? "null" : String(prices[1], 4).c_str(),
		prices[2] == ENTSOE_NO_VALUE ? "null" : String(prices[2], 4).c_str(),
		prices[3] == ENTSOE_NO_VALUE ? "null" : String(prices[3], 4).c_str(),
		prices[4] == ENTSOE_NO_VALUE ? "null" : String(prices[4], 4).c_str(),
		prices[5] == ENTSOE_NO_VALUE ? "null" : String(prices[5], 4).c_str(),
		prices[6] == ENTSOE_NO_VALUE ? "null" : String(prices[6], 4).c_str(),
		prices[7] == ENTSOE_NO_VALUE ? "null" : String(prices[7], 4).c_str(),
		prices[8] == ENTSOE_NO_VALUE ? "null" : String(prices[8], 4).c_str(),
		prices[9] == ENTSOE_NO_VALUE ? "null" : String(prices[9], 4).c_str(),
		prices[10] == ENTSOE_NO_VALUE ? "null" : String(prices[10], 4).c_str(),
		prices[11] == ENTSOE_NO_VALUE ? "null" : String(prices[11], 4).c_str(),
		prices[12] == ENTSOE_NO_VALUE ? "null" : String(prices[12], 4).c_str(),
		prices[13] == ENTSOE_NO_VALUE ? "null" : String(prices[13], 4).c_str(),
		prices[14] == ENTSOE_NO_VALUE ? "null" : String(prices[14], 4).c_str(),
		prices[15] == ENTSOE_NO_VALUE ? "null" : String(prices[15], 4).c_str(),
		prices[16] == ENTSOE_NO_VALUE ? "null" : String(prices[16], 4).c_str(),
		prices[17] == ENTSOE_NO_VALUE ? "null" : String(prices[17], 4).c_str(),
		prices[18] == ENTSOE_NO_VALUE ? "null" : String(prices[18], 4).c_str(),
		prices[19] == ENTSOE_NO_VALUE ? "null" : String(prices[19], 4).c_str(),
		prices[20] == ENTSOE_NO_VALUE ? "null" : String(prices[20], 4).c_str(),
		prices[21] == ENTSOE_NO_VALUE ? "null" : String(prices[21], 4).c_str(),
		prices[22] == ENTSOE_NO_VALUE ? "null" : String(prices[22], 4).c_str(),
		prices[23] == ENTSOE_NO_VALUE ? "null" : String(prices[23], 4).c_str(),
		prices[24] == ENTSOE_NO_VALUE ? "null" : String(prices[24], 4).c_str(),
		prices[25] == ENTSOE_NO_VALUE ? "null" : String(prices[25], 4).c_str(),
		prices[26] == ENTSOE_NO_VALUE ? "null" : String(prices[26], 4).c_str(),
		prices[27] == ENTSOE_NO_VALUE ? "null" : String(prices[27], 4).c_str(),
		prices[28] == ENTSOE_NO_VALUE ? "null" : String(prices[28], 4).c_str(),
		prices[29] == ENTSOE_NO_VALUE ? "null" : String(prices[29], 4).c_str(),
		prices[30] == ENTSOE_NO_VALUE ? "null" : String(prices[30], 4).c_str(),
		prices[31] == ENTSOE_NO_VALUE ? "null" : String(prices[31], 4).c_str(),
		prices[32] == ENTSOE_NO_VALUE ? "null" : String(prices[32], 4).c_str(),
		prices[33] == ENTSOE_NO_VALUE ? "null" : String(prices[33], 4).c_str(),
		prices[34] == ENTSOE_NO_VALUE ? "null" : String(prices[34], 4).c_str(),
		prices[35] == ENTSOE_NO_VALUE ? "null" : String(prices[35], 4).c_str()
	);

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	server.setContentLength(strlen(buf));
	server.send(200, MIME_JSON, buf);
}

void AmsWebServer::temperatureJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /temperature.json over http...\n");

	if(!checkSecurity(2))
		return;

	int count = hw->getTempSensorCount();
	snprintf(buf, 16, "{\"c\":%d,\"s\":[", count);

	for(int i = 0; i < count; i++) {
		TempSensorData* data = hw->getTempSensorData(i);
		if(data == NULL) continue;

		TempSensorConfig* conf = config->getTempSensorConfig(data->address);
		char* pos = buf+strlen(buf);
		snprintf_P(pos, 72, TEMPSENSOR_JSON, 
			i,
			toHex(data->address, 8).c_str(),
			conf == NULL ? "" : String(conf->name).substring(0,16).c_str(),
			conf == NULL || conf->common ? 1 : 0,
			data->lastRead
		);
		delay(10);
	}
	char* pos = buf+strlen(buf);
	snprintf(count == 0 ? pos : pos-1, 8, "]}");

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	server.setContentLength(strlen(buf));
	server.send(200, MIME_JSON, buf);
}

void AmsWebServer::indexHtml() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /index.html over http...\n");

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	if(!checkSecurity(2))
		return;
	server.setContentLength(INDEX_HTML_LEN);
	server.send_P(200, MIME_HTML, INDEX_HTML);
}

void AmsWebServer::indexCss() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /index.css over http...\n");

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	if(!checkSecurity(2))
		return;

	server.setContentLength(INDEX_CSS_LEN);
	server.send_P(200, MIME_CSS, INDEX_CSS);
}

void AmsWebServer::indexJs() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /index.js over http...\n");

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	if(!checkSecurity(2))
		return;

	server.setContentLength(INDEX_JS_LEN);
	server.send_P(200, MIME_JS, INDEX_JS);
}

void AmsWebServer::configurationJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /configuration.json over http...\n");

	server.sendHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_NO_CACHE);
	server.sendHeader(HEADER_PRAGMA, PRAGMA_NO_CACHE);
	server.sendHeader(HEADER_EXPIRES, EXPIRES_OFF);

	if(!checkSecurity(1))
		return;

	DynamicJsonDocument doc(2048);
	doc["version"] = VERSION;

	NtpConfig ntpConfig;
	config->getNtpConfig(ntpConfig);
	WiFiConfig wifiConfig;
	config->getWiFiConfig(wifiConfig);
	WebConfig webConfig;
	config->getWebConfig(webConfig);

	if(ntpConfig.offset == 0 && ntpConfig.summerOffset == 0)
		doc["g"]["t"] = "UTC";
	else if(ntpConfig.offset == 360 && ntpConfig.summerOffset == 360)
		doc["g"]["t"] = "CET/CEST";

	doc["g"]["h"] = wifiConfig.hostname;
	doc["g"]["s"] = webConfig.security;
	doc["g"]["u"] = webConfig.username;
	doc["g"]["p"] = strlen(webConfig.password) > 0 ? "***" : "";

	bool encen = false;
	for(uint8_t i = 0; i < 16; i++) {
		if(meterConfig->encryptionKey[i] > 0) {
			encen = true;
		}
	}

	config->getMeterConfig(*meterConfig);
	doc["m"]["b"] = meterConfig->baud;
	doc["m"]["p"] = meterConfig->parity;
	doc["m"]["i"] = meterConfig->invert;
	doc["m"]["d"] = meterConfig->distributionSystem;
	doc["m"]["f"] = meterConfig->mainFuse;
	doc["m"]["r"] = meterConfig->productionCapacity;
	doc["m"]["e"]["e"] = encen;
	doc["m"]["e"]["k"] = toHex(meterConfig->encryptionKey, 16);
	doc["m"]["e"]["a"] = toHex(meterConfig->authenticationKey, 16);
	doc["m"]["m"]["e"] = meterConfig->wattageMultiplier > 1 || meterConfig->voltageMultiplier > 1 || meterConfig->amperageMultiplier > 1 || meterConfig->accumulatedMultiplier > 1;
	doc["m"]["m"]["w"] = meterConfig->wattageMultiplier / 1000.0;
	doc["m"]["m"]["v"] = meterConfig->voltageMultiplier / 1000.0;
	doc["m"]["m"]["a"] = meterConfig->amperageMultiplier / 1000.0;
	doc["m"]["m"]["c"] = meterConfig->accumulatedMultiplier / 1000.0;

	EnergyAccountingConfig eac;
	config->getEnergyAccountingConfig(eac);
	doc["t"]["t"][0] = eac.thresholds[0];
	doc["t"]["t"][1] = eac.thresholds[1];
	doc["t"]["t"][2] = eac.thresholds[2];
	doc["t"]["t"][3] = eac.thresholds[3];
	doc["t"]["t"][4] = eac.thresholds[4];
	doc["t"]["t"][5] = eac.thresholds[5];
	doc["t"]["t"][6] = eac.thresholds[6];
	doc["t"]["t"][7] = eac.thresholds[7];
	doc["t"]["t"][8] = eac.thresholds[8];
	doc["t"]["t"][9] = eac.thresholds[9];
	doc["t"]["h"] = eac.hours;

	doc["w"]["s"] = wifiConfig.ssid;
	doc["w"]["p"] = strlen(wifiConfig.psk) > 0 ? "***" : "";
	doc["w"]["w"] = wifiConfig.power / 10.0;
	doc["w"]["z"] = wifiConfig.sleep;

	doc["n"]["m"] = strlen(wifiConfig.ip) > 0 ? "static" : "dhcp";
	doc["n"]["i"] = wifiConfig.ip;
	doc["n"]["s"] = wifiConfig.subnet;
	doc["n"]["g"] = wifiConfig.gateway;
	doc["n"]["d1"] = wifiConfig.dns1;
	doc["n"]["d2"] = wifiConfig.dns2;
	doc["n"]["d"] = wifiConfig.mdns;
	doc["n"]["n1"] = ntpConfig.server;
	doc["n"]["h"] = ntpConfig.dhcp;

	MqttConfig mqttConfig;
	config->getMqttConfig(mqttConfig);
	doc["q"]["h"] = mqttConfig.host;
	doc["q"]["p"] = mqttConfig.port;
	doc["q"]["u"] = mqttConfig.username;
	doc["q"]["a"] = strlen(mqttConfig.password) > 0 ? "***" : "";
	doc["q"]["c"] = mqttConfig.clientId;
	doc["q"]["b"] = mqttConfig.publishTopic;
	doc["q"]["m"] = mqttConfig.payloadFormat;
	doc["q"]["s"]["e"] = mqttConfig.ssl;

	if(LittleFS.begin()) {
		doc["q"]["s"]["c"] = LittleFS.exists(FILE_MQTT_CA);
		doc["q"]["s"]["r"] = LittleFS.exists(FILE_MQTT_CERT);
		doc["q"]["s"]["k"] = LittleFS.exists(FILE_MQTT_KEY);
		LittleFS.end();
	} else {
		doc["q"]["s"]["c"] = false;
		doc["q"]["s"]["r"] = false;
		doc["q"]["s"]["k"] = false;
	}

	EntsoeConfig entsoe;
	config->getEntsoeConfig(entsoe);
	doc["p"]["e"] = strlen(entsoe.token) > 0;
	doc["p"]["t"] = entsoe.token;
	doc["p"]["r"] = entsoe.area;
	doc["p"]["c"] = entsoe.currency;
	doc["p"]["m"] = entsoe.multiplier / 1000.0;

	DebugConfig debugConfig;
	config->getDebugConfig(debugConfig);
	doc["d"]["s"] = debugConfig.serial;
	doc["d"]["t"] = debugConfig.telnet;
	doc["d"]["l"] = debugConfig.level;

	GpioConfig gpioConfig;
	config->getGpioConfig(gpioConfig);
	if(gpioConfig.hanPin == 0xff)
		doc["i"]["h"] = nullptr;
	else
		doc["i"]["h"] = gpioConfig.hanPin;
	
	if(gpioConfig.apPin == 0xff)
		doc["i"]["a"] = nullptr;
	else
		doc["i"]["a"] = gpioConfig.apPin;
	
	if(gpioConfig.ledPin == 0xff)
		doc["i"]["l"]["p"] = nullptr;
	else
		doc["i"]["l"]["p"] = gpioConfig.ledPin;
	
	doc["i"]["l"]["i"] = gpioConfig.ledInverted;
	
	if(gpioConfig.ledPinRed == 0xff)
		doc["i"]["r"]["r"] = nullptr;
	else
		doc["i"]["r"]["r"] = gpioConfig.ledPinRed;

	if(gpioConfig.ledPinGreen == 0xff)
		doc["i"]["r"]["g"] = nullptr;
	else
		doc["i"]["r"]["g"] = gpioConfig.ledPinGreen;

	if(gpioConfig.ledPinBlue == 0xff)
		doc["i"]["r"]["b"] = nullptr;
	else
		doc["i"]["r"]["b"] = gpioConfig.ledPinBlue;

	doc["i"]["r"]["i"] = gpioConfig.ledRgbInverted;

	if(gpioConfig.tempSensorPin == 0xff)
		doc["i"]["t"]["d"] = nullptr;
	else
		doc["i"]["t"]["d"] = gpioConfig.tempSensorPin;

	if(gpioConfig.tempAnalogSensorPin == 0xff)
		doc["i"]["t"]["a"] = nullptr;
	else
		doc["i"]["t"]["a"] = gpioConfig.tempAnalogSensorPin;

	if(gpioConfig.vccPin == 0xff)
		doc["i"]["v"]["p"] = nullptr;
	else
		doc["i"]["v"]["p"] = gpioConfig.vccPin;

	if(gpioConfig.vccOffset == 0)
		doc["i"]["v"]["o"] = nullptr;
	else
		doc["i"]["v"]["o"] = gpioConfig.vccOffset / 100.0;

	if(gpioConfig.vccMultiplier == 0)
		doc["i"]["v"]["m"] = nullptr;
	else
		doc["i"]["v"]["m"] = gpioConfig.vccMultiplier / 1000.0;

	if(gpioConfig.vccResistorVcc == 0)
		doc["i"]["v"]["d"]["v"] = nullptr;
	else
		doc["i"]["v"]["d"]["v"] = gpioConfig.vccResistorVcc;

	if(gpioConfig.vccResistorGnd == 0)
		doc["i"]["v"]["d"]["g"] = nullptr;
	else
		doc["i"]["v"]["d"]["g"] = gpioConfig.vccResistorGnd;

	if(gpioConfig.vccBootLimit == 0)
		doc["i"]["v"]["b"] = nullptr;
	else
		doc["i"]["v"]["b"] = gpioConfig.vccBootLimit / 10.0;

	serializeJson(doc, buf, BufferSize);
	server.send(200, MIME_JSON, buf);
}
void AmsWebServer::handleSave() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Handling save method from http"));
	if(!checkSecurity(1))
		return;

	bool success = true;
	if(server.hasArg(F("v")) && server.arg(F("v")) == F("true")) {
		int boardType = server.arg(F("b")).toInt();
		int hanPin = server.arg(F("h")).toInt();

		#if defined(CONFIG_IDF_TARGET_ESP32S2)
			switch(boardType) {
				case 5: // Pow-K+
				case 7: // Pow-U+
				case 6: // Pow-P1
					config->clearGpio(*gpioConfig);
					gpioConfig->hanPin = 16;
					gpioConfig->apPin = 0;
					gpioConfig->ledPinRed = 13;
					gpioConfig->ledPinGreen = 14;
					gpioConfig->ledRgbInverted = true;
					gpioConfig->vccPin = 10;
					gpioConfig->vccResistorGnd = 22;
					gpioConfig->vccResistorVcc = 33;
					break;
				case 51: // Wemos S2 mini
					gpioConfig->ledPin = 15;
					gpioConfig->ledInverted = false;
					gpioConfig->apPin = 0;
				case 50: // Generic ESP32-S2
					gpioConfig->hanPin = hanPin > 0 ? hanPin : 18;
					break;
				default:
					success = false;
			}
		#elif defined(CONFIG_IDF_TARGET_ESP32C3)
		#elif defined(ESP32)
			switch(boardType) {
				case 201: // D32
					gpioConfig->hanPin = hanPin > 0 ? hanPin : 16;
					gpioConfig->apPin = 4;
					gpioConfig->ledPin = 5;
					gpioConfig->ledInverted = true;
					break;
				case 202: // Feather
				case 203: // DevKitC
				case 200: // ESP32
					gpioConfig->hanPin = hanPin > 0 ? hanPin : 16;
					gpioConfig->ledPin = 2;
					gpioConfig->ledInverted = false;
					break;
				default:
					success = false;
			}
		#elif defined(ESP8266)
			switch(boardType) {
				case 2: // spenceme
					config->clearGpio(*gpioConfig);
					gpioConfig->vccBootLimit = 33;
				case 0: // roarfred
					gpioConfig->hanPin = 3;
					gpioConfig->apPin = 0;
					gpioConfig->ledPin = 2;
					gpioConfig->ledInverted = true;
					gpioConfig->tempSensorPin = 5;
					break;
				case 1: // Arnio Kamstrup
				case 3: // Pow-K UART0
				case 4: // Pow-U UART0
					config->clearGpio(*gpioConfig);
					gpioConfig->hanPin = 3;
					gpioConfig->apPin = 0;
					gpioConfig->ledPin = 2;
					gpioConfig->ledInverted = true;
					gpioConfig->ledPinRed = 13;
					gpioConfig->ledPinGreen = 14;
					gpioConfig->ledRgbInverted = true;
					break;
				case 5: // Pow-K GPIO12
				case 7: // Pow-U GPIO12
					config->clearGpio(*gpioConfig);
					gpioConfig->hanPin = 12;
					gpioConfig->apPin = 0;
					gpioConfig->ledPin = 2;
					gpioConfig->ledInverted = true;
					gpioConfig->ledPinRed = 13;
					gpioConfig->ledPinGreen = 14;
					gpioConfig->ledRgbInverted = true;
					break;
				case 101: // D1
					gpioConfig->hanPin = hanPin > 0 ? hanPin : 5;
					gpioConfig->apPin = 4;
					gpioConfig->ledPin = 2;
					gpioConfig->ledInverted = true;
					gpioConfig->vccMultiplier = 1100;
					break;
				case 100: // ESP8266
					gpioConfig->hanPin = hanPin > 0 ? hanPin : 3;
					gpioConfig->ledPin = 2;
					gpioConfig->ledInverted = true;
					break;
				default:
					success = false;
			}
		#endif

		SystemConfig sys;
		config->getSystemConfig(sys);
		sys.boardType = success ? boardType : 0xFF;
		sys.vendorConfigured = success;
		config->setSystemConfig(sys);
	}

	if(server.hasArg(F("s")) && server.arg(F("s")) == F("true")) {
		SystemConfig sys;
		config->getSystemConfig(sys);

		config->clear();

		WiFiConfig wifi;
		config->clearWifi(wifi);

		strcpy(wifi.ssid, server.arg(F("ss")).c_str());

		String psk = server.arg(F("sp"));
		if(!psk.equals("***")) {
			strcpy(wifi.psk, psk.c_str());
		}
		wifi.mode = 1; // WIFI_STA

		if(server.hasArg(F("sm")) && server.arg(F("sm")) == "static") {
			strcpy(wifi.ip, server.arg(F("si")).c_str());
			strcpy(wifi.gateway, server.arg(F("sg")).c_str());
			strcpy(wifi.subnet, server.arg(F("su")).c_str());
			strcpy(wifi.dns1, server.arg(F("sd")).c_str());
		}

		if(server.hasArg(F("sh")) && !server.arg(F("sh")).isEmpty()) {
			strcpy(wifi.hostname, server.arg(F("sh")).c_str());
			wifi.mdns = true;
		} else {
			wifi.mdns = false;
		}
		
		switch(sys.boardType) {
			case 6: // Pow-P1
				meterConfig->baud = 115200;
				meterConfig->parity = 3; // 8N1
				break;
			case 3: // Pow-K UART0
			case 5: // Pow-K+
				meterConfig->parity = 3; // 8N1
			case 2: // spenceme
			case 50: // Generic ESP32-S2
			case 51: // Wemos S2 mini
				meterConfig->baud = 2400;
				wifi.sleep = 1; // Modem sleep
				break;
			case 4: // Pow-U UART0
			case 7: // Pow-U+
				wifi.sleep = 2; // Light sleep
				break;
		}
		config->setWiFiConfig(wifi);
		config->setMeterConfig(*meterConfig);
		
		sys.userConfigured = success;
		//TODO sys.country 
		sys.dataCollectionConsent = server.hasArg(F("sf")) && server.arg(F("sf")) == F("true") ? 1 : 2;
		config->setSystemConfig(sys);
	}

	if(server.hasArg(F("m")) && server.arg(F("m")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received meter config"));
		config->getMeterConfig(*meterConfig);
		meterConfig->baud = server.arg(F("mb")).toInt();
		meterConfig->parity = server.arg(F("mp")).toInt();
		meterConfig->invert = server.hasArg(F("mi")) && server.arg(F("mi")) == F("true");
		meterConfig->distributionSystem = server.arg(F("md")).toInt();
		meterConfig->mainFuse = server.arg(F("mf")).toInt();
		meterConfig->productionCapacity = server.arg(F("mr")).toInt();
		maxPwr = 0;

		String encryptionKeyHex = server.arg(F("mek"));
		if(!encryptionKeyHex.isEmpty()) {
			encryptionKeyHex.replace(F("0x"), F(""));
			fromHex(meterConfig->encryptionKey, encryptionKeyHex, 16);
		}

		String authenticationKeyHex = server.arg(F("mea"));
		if(!authenticationKeyHex.isEmpty()) {
			authenticationKeyHex.replace(F("0x"), F(""));
			fromHex(meterConfig->authenticationKey, authenticationKeyHex, 16);
		}

		meterConfig->wattageMultiplier = server.arg(F("mmw")).toDouble() * 1000;
		meterConfig->voltageMultiplier = server.arg(F("mmv")).toDouble() * 1000;
		meterConfig->amperageMultiplier = server.arg(F("mma")).toDouble() * 1000;
		meterConfig->accumulatedMultiplier = server.arg(F("mmc")).toDouble() * 1000;
		config->setMeterConfig(*meterConfig);
	}

	if(server.hasArg(F("w")) && server.arg(F("w")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received WiFi config"));
		WiFiConfig wifi;
		config->getWiFiConfig(wifi);
		strcpy(wifi.ssid, server.arg(F("ws")).c_str());
		String psk = server.arg(F("wp"));
		if(!psk.equals("***")) {
			strcpy(wifi.psk, psk.c_str());
		}
		wifi.power = server.arg(F("ww")).toFloat() * 10;
		wifi.sleep = server.arg(F("wz")).toInt();
		config->setWiFiConfig(wifi);

		if(server.hasArg(F("nm")) && server.arg(F("nm")) == "static") {
			strcpy(wifi.ip, server.arg(F("ni")).c_str());
			strcpy(wifi.gateway, server.arg(F("ng")).c_str());
			strcpy(wifi.subnet, server.arg(F("ns")).c_str());
			strcpy(wifi.dns1, server.arg(F("nd1")).c_str());
			strcpy(wifi.dns2, server.arg(F("nd2")).c_str());
		}
		wifi.mdns = server.hasArg(F("nd")) && server.arg(F("nd")) == F("true");
		config->setWiFiConfig(wifi);
	}

	if(server.hasArg(F("ntp")) && server.arg(F("ntp")) == F("true")) {
		NtpConfig ntp;
		config->getNtpConfig(ntp);
		ntp.enable = true;
		ntp.dhcp = server.hasArg(F("ntpd")) && server.arg(F("ntpd")) == F("true");
		strcpy(ntp.server, server.arg(F("ntph")).c_str());
		config->setNtpConfig(ntp);
	}

	if(server.hasArg(F("q")) && server.arg(F("q")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received MQTT config"));
		MqttConfig mqtt;
		if(server.hasArg(F("qh")) && !server.arg(F("qh")).isEmpty()) {
			strcpy(mqtt.host, server.arg(F("qh")).c_str());
			strcpy(mqtt.clientId, server.arg(F("qc")).c_str());
			strcpy(mqtt.publishTopic, server.arg(F("qb")).c_str());
			strcpy(mqtt.subscribeTopic, server.arg(F("qr")).c_str());
			strcpy(mqtt.username, server.arg(F("qu")).c_str());
			String pass = server.arg(F("qp"));
			if(!pass.equals("***")) {
				strcpy(mqtt.password, pass.c_str());
			}
			mqtt.payloadFormat = server.arg(F("qm")).toInt();
			mqtt.ssl = server.arg(F("qs")) == F("true");

			mqtt.port = server.arg(F("qp")).toInt();
			if(mqtt.port == 0) {
				mqtt.port = mqtt.ssl ? 8883 : 1883;
			}
		} else {
			config->clearMqtt(mqtt);
		}
		config->setMqttConfig(mqtt);
	}

	if(server.hasArg(F("dc")) && server.arg(F("dc")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received Domoticz config"));
		DomoticzConfig domo {
			static_cast<uint16_t>(server.arg(F("elidx")).toInt()),
			static_cast<uint16_t>(server.arg(F("vl1idx")).toInt()),
			static_cast<uint16_t>(server.arg(F("vl2idx")).toInt()),
			static_cast<uint16_t>(server.arg(F("vl3idx")).toInt()),
			static_cast<uint16_t>(server.arg(F("cl1idx")).toInt())
		};
		config->setDomoticzConfig(domo);
	}


	if(server.hasArg(F("g")) && server.arg(F("g")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received web config"));
		webConfig.security = server.arg(F("gs")).toInt();
		if(webConfig.security > 0) {
			strcpy(webConfig.username, server.arg(F("gu")).c_str());
			String pass = server.arg(F("gp"));
			if(!pass.equals("***")) {
				strcpy(webConfig.password, pass.c_str());
			}
			debugger->setPassword(webConfig.password);
		} else {
			strcpy_P(webConfig.username, PSTR(""));
			strcpy_P(webConfig.password, PSTR(""));
			debugger->setPassword(F(""));
		}
		config->setWebConfig(webConfig);

		WiFiConfig wifi;
		config->getWiFiConfig(wifi);
		if(server.hasArg(F("gh")) && !server.arg(F("gh")).isEmpty()) {
			strcpy(wifi.hostname, server.arg(F("gh")).c_str());
		}
		config->setWiFiConfig(wifi);

		NtpConfig ntp;
		config->getNtpConfig(ntp);
		String tz = server.arg(F("gt"));
		if(tz.equals("UTC")) {
			ntp.offset = 0;
			ntp.summerOffset = 0;
		} else if(tz.equals("CET/CEST")) {
			ntp.offset = 360;
			ntp.summerOffset = 360;
		}
		config->setNtpConfig(ntp);
	}

	if(server.hasArg(F("i")) && server.arg(F("i")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received GPIO config"));
		gpioConfig->hanPin = server.hasArg(F("ih")) && !server.arg(F("ih")).isEmpty() ? server.arg(F("ih")).toInt() : 3;
		gpioConfig->ledPin = server.hasArg(F("ilp")) && !server.arg(F("ilp")).isEmpty() ? server.arg(F("ilp")).toInt() : 0xFF;
		gpioConfig->ledInverted = server.hasArg(F("ili")) && server.arg(F("ili")) == F("true");
		gpioConfig->ledPinRed = server.hasArg(F("irr")) && !server.arg(F("irr")).isEmpty() ? server.arg(F("irr")).toInt() : 0xFF;
		gpioConfig->ledPinGreen = server.hasArg(F("irg")) && !server.arg(F("irg")).isEmpty() ? server.arg(F("irg")).toInt() : 0xFF;
		gpioConfig->ledPinBlue = server.hasArg(F("irb")) && !server.arg(F("irb")).isEmpty() ? server.arg(F("irb")).toInt() : 0xFF;
		gpioConfig->ledRgbInverted = server.hasArg(F("iri")) && server.arg(F("iri")) == F("true");
		gpioConfig->apPin = server.hasArg(F("ia")) && !server.arg(F("ia")).isEmpty() ? server.arg(F("ia")).toInt() : 0xFF;
		gpioConfig->tempSensorPin = server.hasArg(F("itd")) && !server.arg(F("itd")).isEmpty() ?server.arg(F("itd")).toInt() : 0xFF;
		gpioConfig->tempAnalogSensorPin = server.hasArg(F("ita")) && !server.arg(F("ita")).isEmpty() ?server.arg(F("ita")).toInt() : 0xFF;
		gpioConfig->vccPin = server.hasArg(F("ivp")) && !server.arg(F("ivp")).isEmpty() ? server.arg(F("ivp")).toInt() : 0xFF;
		gpioConfig->vccOffset = server.hasArg(F("ivo")) && !server.arg(F("ivo")).isEmpty() ? server.arg(F("ivo")).toFloat() * 100 : 0;
		gpioConfig->vccMultiplier = server.hasArg(F("ivm")) && !server.arg(F("ivm")).isEmpty() ? server.arg(F("ivm")).toFloat() * 1000 : 1000;
		gpioConfig->vccBootLimit = server.hasArg(F("ivb")) && !server.arg(F("ivb")).isEmpty() ? server.arg(F("ivb")).toFloat() * 10 : 0;
		gpioConfig->vccResistorGnd = server.hasArg(F("ivdg")) && !server.arg(F("ivdg")).isEmpty() ? server.arg(F("ivdg")).toInt() : 0;
		gpioConfig->vccResistorVcc = server.hasArg(F("ivdv")) && !server.arg(F("ivdv")).isEmpty() ? server.arg(F("ivdv")).toInt() : 0;
		config->setGpioConfig(*gpioConfig);
	}

	if(server.hasArg(F("d")) && server.arg(F("d")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received Debug config"));
		DebugConfig debug;
		config->getDebugConfig(debug);
		bool active = debug.serial || debug.telnet;

		debug.telnet = server.hasArg(F("dt")) && server.arg(F("dt")) == F("true");
		debug.serial = server.hasArg(F("ds")) && server.arg(F("ds")) == F("true");
		debug.level = server.arg(F("dl")).toInt();

		if(debug.telnet || debug.serial) {
			if(webConfig.security > 0) {
				debugger->setPassword(webConfig.password);
			} else {
				debugger->setPassword(F(""));
			}
			debugger->setSerialEnabled(debug.serial);
			WiFiConfig wifi;
			if(config->getWiFiConfig(wifi) && strlen(wifi.hostname) > 0) {
				debugger->begin(wifi.hostname, (uint8_t) debug.level);
				if(!debug.telnet) {
					debugger->stop();
				}
			}
		} else if(active) {
			performRestart = true;
		}
		config->setDebugConfig(debug);
	}

	if(server.hasArg(F("p")) && server.arg(F("p")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received price API config"));
		EntsoeConfig entsoe;
		strcpy(entsoe.token, server.arg(F("pt")).c_str());
		strcpy(entsoe.area, server.arg(F("pr")).c_str());
		strcpy(entsoe.currency, server.arg(F("pc")).c_str());
		entsoe.multiplier = server.arg(F("pm")).toFloat() * 1000;
		config->setEntsoeConfig(entsoe);
	}

	if(server.hasArg(F("t")) && server.arg(F("t")) == F("true")) {
		if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf(PSTR("Received energy accounting config"));
		EnergyAccountingConfig eac;
		eac.thresholds[0] = server.arg(F("t0")).toInt();
		eac.thresholds[1] = server.arg(F("t1")).toInt();
		eac.thresholds[2] = server.arg(F("t2")).toInt();
		eac.thresholds[3] = server.arg(F("t3")).toInt();
		eac.thresholds[4] = server.arg(F("t4")).toInt();
		eac.thresholds[5] = server.arg(F("t5")).toInt();
		eac.thresholds[6] = server.arg(F("t6")).toInt();
		eac.thresholds[7] = server.arg(F("t7")).toInt();
		eac.thresholds[8] = server.arg(F("t8")).toInt();
		eac.hours = server.arg(F("th")).toInt();
		config->setEnergyAccountingConfig(eac);
	}

	if(debugger->isActive(RemoteDebug::INFO)) debugger->printf(PSTR("Saving configuration now..."));

	DynamicJsonDocument doc(128);
	if (config->save()) {
		doc["success"] = success;
		if(debugger->isActive(RemoteDebug::INFO)) debugger->printf(PSTR("Successfully saved."));
		if(config->isWifiChanged() || performRestart) {
			performRestart = true;
			doc["reboot"] = true;
		} else {
			doc["reboot"] = false;
			hw->setup(gpioConfig, config);
		}
	} else {
		doc["success"] = false;
		doc["reboot"] = false;
	}
	serializeJson(doc, buf, BufferSize);
	server.send(200, MIME_JSON, buf);

	server.handleClient();
	delay(250);

	if(performRestart || rebootForUpgrade) {
		if(ds != NULL) {
			ds->save();
		}
		if(debugger->isActive(RemoteDebug::INFO)) debugger->printf(PSTR("Rebooting"));
		delay(1000);
		#if defined(ESP8266)
			ESP.reset();
		#elif defined(ESP32)
			ESP.restart();
		#endif
		performRestart = false;
	}
}

void AmsWebServer::wifiScanJson() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /wifiscan.json over http...\n");

	DynamicJsonDocument doc(512);

	serializeJson(doc, buf, BufferSize);
	server.send(200, MIME_JSON, buf);
}

void AmsWebServer::reboot() {
	if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf("Serving /reboot over http...\n");

	DynamicJsonDocument doc(128);
	doc["reboot"] = true;

	serializeJson(doc, buf, BufferSize);
	server.send(200, MIME_JSON, buf);

	server.handleClient();
	delay(250);

	if(debugger->isActive(RemoteDebug::INFO)) debugger->printf(PSTR("Rebooting"));
	delay(1000);
	#if defined(ESP8266)
		ESP.reset();
	#elif defined(ESP32)
		ESP.restart();
	#endif
	performRestart = false;
}