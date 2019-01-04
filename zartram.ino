/* Projekt Pawe³ Zarembski */
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp8266.h>
//#include <BlynkSimpleStream.h>
#include "DHT.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LCD.h>
#include <Ticker.h>

#define N 30 // rozmiar tablicy z czasem

/* define Blynk */
#define BLYNK_PRINT Serial
#define PIN_PIRSEN V3 // virtual blynk pin dla terminala
#define PIN_TERMIN V4 // virtual blynk pin dla terminala
#define PIN_TEMPER V5 // virtual blynk pin dla temperatur
#define PIN_UPTIME V6 // virtual blynk pin dla czasu
#define PIN_HUMINI V7 // virtual blynk pin dla wilgotnoœci

/* define PIR SENSOR */
#define PIRPIN D6

/* define DHT */
#define DHTPIN D7     // pin na p³ytce
#define DHTTYPE DHT22 // typ czujnika dht22

/* define LCD */
#define I2C_ADDR 0x3F
#define BACKLIGHT_PIN 3
#define En_pin 2
#define Rw_pin 1
#define Rs_pin 0
#define D4_pin 4
#define D5_pin 5
#define D6_pin 6
#define D7_pin 7

Ticker WiFi_timer;	// ticker stanu do odczytu stanu tramwajow
Ticker LCD_change;	// ticker stanu do przewijania LCD
volatile uint8_t WiFi_timer_enable = 1;
volatile uint8_t LCD_ticker_i = 0;

/* tablice do przetwarzania wynikow */
int id_table[N];
int route_table[N];
char estimated_table[N][6];
int delay_table[N];
int tram_number;

char auth[] = "04bb63fb1dac49789aaa55901ad59468";	// blynk auth key
char ssid[] = "Maslan_zarembianu";					// wifi
char pass[] = "maslanka";							// pass

DHT dht(DHTPIN, DHTTYPE); // obiekt dla czujnika dht
float h;                  // zmienna dla temperatury
float t;                  // zmienna dla wilgotnoœci
int PIRout;               // stan pinu czujnika PIR

/* obiekt wyœwietlacza LCD */
LiquidCrystal_I2C lcd(I2C_ADDR, En_pin, Rw_pin, Rs_pin, D4_pin, D5_pin, D6_pin, D7_pin);

/* timer dla pobiernia danych o tramwajach */
void changeState() {
	WiFi_timer_enable = 1;
}

void moveLCD() {
	LCD_ticker_i++;
	if (LCD_ticker_i = tram_number) {
		LCD_ticker_i = 0;
	}
}

/* zapis zmiennych do wirtualnych pinów */
BLYNK_READ(PIN_TEMPER) {
	Blynk.virtualWrite(PIN_TEMPER, t);
}
BLYNK_READ(PIN_UPTIME) {
	Blynk.virtualWrite(PIN_UPTIME, millis() / 1000);
}
BLYNK_READ(PIN_HUMINI) {
	Blynk.virtualWrite(PIN_HUMINI, h);
}
/* LED w app Blynk */
WidgetLED pir_led(PIN_PIRSEN);

/* ³¹czenie i ponowne ³¹czenie blynk */
WiFiClient wifiClient;
bool connectBlynk()
{
	wifiClient.stop();
	return wifiClient.connect(BLYNK_DEFAULT_DOMAIN, BLYNK_DEFAULT_PORT);
}

/* ³¹czenie i ponowne ³¹czenie wifi */
void connectWiFi()
{
	if (pass && strlen(pass)) {	// polacz
		WiFi.begin((char*)ssid, (char*)pass);
	}
	else {
		WiFi.begin((char*)ssid);
	}

	
	while (WiFi.status() != WL_CONNECTED) { // czekaj na polaczenie
		delay(500);
		digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) ^ 1);
	}
}

void setup()
{
	/* inicjalizacja lcd */
	lcd.begin(16, 2);
	lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
	lcd.setBacklight(HIGH);
	lcd.home();
	lcd.setCursor(0, 0);
	lcd.print("LCD");

	/* inicjalizacja Led */
	pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
	pinMode(PIRPIN, INPUT);
	pinMode(DHTPIN, INPUT);
	lcd.print("|LED");

	/* inicjalizacja wifi i blynk */
	connectWiFi();
	lcd.print("|WiFi");
	connectBlynk();
	Blynk.config(auth);
	lcd.setCursor(0, 1);
	lcd.print("BLYNK");
	Blynk.connect();
	lcd.print("1");
	digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) ^ 1);

	/* inicjalizacja czujnika DHT */
	dht.begin();
	lcd.print("|DHT");
	digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) ^ 1);

	// Timer
	WiFi_timer.attach(60, changeState);
	LCD_change.attach(3, moveLCD);
	lcd.print("||START");
	lcd.clear();
	lcd.print("ZARTRAM (TM)");
}


void loop()
{
	/* pomiar DHT22 */
	h = dht.readHumidity();
	t = dht.readTemperature();
	lcd.setCursor(0, 0);
	lcd.print("T:");
	lcd.print(t);
	lcd.print(" W:");
	lcd.print(h);
	
	/* odczyt PIR */
	PIRout = digitalRead(PIRPIN);
	if (PIRout == 1) {
		pir_led.on();
	}
	else {
		pir_led.off();
	}

	/* obs³uga tramwajów */
	if ((WiFi.status() == WL_CONNECTED) && (WiFi_timer_enable == 1)) {
		HTTPClient http;  // obiekt klienta http
		http.begin("http://87.98.237.99:88/delays?stopId=2074"); // get st¹d
		int httpCode = http.GET();

		if (httpCode > 0) {
			int i = 0;
			/* Parsing */
			const size_t bufferSize = JSON_ARRAY_SIZE(11) + JSON_OBJECT_SIZE(2) + 11 * JSON_OBJECT_SIZE(12); // z internetu
			DynamicJsonBuffer jsonBuffer;                                 // obiekt bufora
			JsonObject& root = jsonBuffer.parseObject(http.getString());  // pobranie ca³ego stringa
			JsonArray& delay = root["delay"];                             // stworzenie tablicy dla "delay"

			for (auto& delays : delay) {								// dla obiektu w tablicy sprawdŸ
				int id = delays["id"];                                  // parametr id
				int routeId = delays["routeId"];                        // numer linii
				const char* estimatedTime = delays["estimatedTime"];    // oczekiwany czas przyjazdu
				int delayInSeconds = delays["delayInSeconds"];          // opóŸnienie w sekundach (+ lub -)

				/* Blynk Serial */
				Blynk.virtualWrite(PIN_TERMIN, routeId);
				Blynk.virtualWrite(PIN_TERMIN, " bedzie o ");
				Blynk.virtualWrite(PIN_TERMIN, estimatedTime);
				Blynk.virtualWrite(PIN_TERMIN, " spozniony ");
				Blynk.virtualWrite(PIN_TERMIN, delayInSeconds);
				Blynk.virtualWrite(PIN_TERMIN, " s\n");

				/* Obs³uga wyników */
				id_table[i] = id;
				route_table[i] = routeId;
				estimated_table[i][0] = *estimatedTime;
				delay_table[i] = delayInSeconds;

				/* obliczanie ilosci wpisow */
				i++;
				tram_number = i;
				if (tram_number > N) {
					break;
				}
				Blynk.virtualWrite(PIN_TERMIN, "-----------------------\n");
			}
		}
		http.end(); // roz³¹czenie z tramwajem

		/* migniecie dioda co cykl */
		digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) ^ 1);
		WiFi_timer_enable = 0; // zmiana stanu
	}

	/* wyswietlanie LCD */
	lcd.setCursor(0, 1);
	lcd.print(route_table[LCD_ticker_i]);
	lcd.print("-");
	for (int j = 0; j < 6; j++) {
		lcd.print(estimated_table[LCD_ticker_i + 1][j]);
	}
	if (tram_number > 1) { // przypadek jednego tramwaju na liscie
		lcd.print("||");
		lcd.print(route_table[LCD_ticker_i + 1]);
		lcd.print("-");
		for (int j = 0; j < 6; j++) {
			lcd.print(estimated_table[LCD_ticker_i + 1][j]);
		}
	}
	else { // pusto    
		lcd.print("        ");
	}

	// Reconnect WiFi
	if (WiFi.status() != WL_CONNECTED) {
		connectWiFi();
		Blynk.virtualWrite(PIN_TERMIN, "\t> WiFi reconnect <\n ");
		return;
	}
	// Reconnect to Blynk Cloud
	if (!wifiClient.connected()) {
		connectBlynk();
		Blynk.virtualWrite(PIN_TERMIN, "\t> Blynk reconnect <\n ");
		return;
	}
	Blynk.run();
}