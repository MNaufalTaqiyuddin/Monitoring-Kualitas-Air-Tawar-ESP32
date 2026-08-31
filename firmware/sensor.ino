#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"
#include <Fuzzy.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// ======Informasi WiFi=====
//const char* ssid = "Ruang bawah";
//const char* password = "samudra139";
const char* ssid = "POCO X6 5G";
const char* password = "99999999";
//const char* ssid = "TP-Link_FC1C";
//const char* password = "08122889865";
//-----------------------------------------------------------------------------------------------------------

// =====Informasi Firebase=====
#define DATABASE_URL "https://iotesp32-c18ff-default-rtdb.firebaseio.com/" 
//https://iotesp32-c18ff-default-rtdb.firebaseio.com/
//https://esp32iot-peternakan-default-rtdb.firebaseio.com/
#define API_KEY "AIzaSyDeQZz7zcUD110ATmzYb0FHne9UQiQW8fo" 
//t7ww2WnWbTxs6MwxCqUPFvNhAUaBTnEAzeG2go8l
//AIzaSyAfvMikBKa9GlwB46nsz0HH21BGDlm3d_M
//AIzaSyDeQZz7zcUD110ATmzYb0FHne9UQiQW8fo
#define DATABASE_SECRET "GnudCCrWqHcx6UuNDjb9zf3DhwWegnos4uL5FoyI"

WiFiClient espClient;
// dapatkan waktu
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;   // WIB
const int daylightOffset_sec = 0;
const long utcOffsetInSeconds = 7 * 3600;   // GMT+7

String lastUpload = "";
String lastDailyUpload = "";
float penilaianBulanan = 0;
String lastCleanup = "";
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
//-----------------------------------------------------------------------------------------------------------

//===== Konfigurasi LCD=====
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Alamat I2C LCD, Jumlah kolom, Jumlah baris
//-------------------------------------------------------------------------------------------------------------

// =====Konfigurasi sensor suhu DS18B20=====
const int ONE_WIRE_BUS = 4;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
//=====Sensor amonia dan ph=====
const int mq135Pin = 34; // Pin analog sensor MQ-135
const int ph_Pin = 35;
const float R0 = 40000; // Nilai resistansi pada kondisi bersih
float Po = 0;
float PH_step;
int nilai_analog_PH;
double TeganganPh;
float PH4 = 11.20;
float PH7 = 9.96;
float voltage;
//-----------------------------------------------------------------------------------------------------------------


//=====FUZZY=====
Fuzzy *fuzzy = new Fuzzy();


//INPUT FUZZY PH
FuzzySet *SR = new FuzzySet( 0, 0, 4, 5);
FuzzySet *R = new FuzzySet(4.5, 5, 6, 6.5);
FuzzySet *B = new FuzzySet(6, 6.5, 8.5, 9);
FuzzySet *T = new FuzzySet(8.5, 9, 9.5, 10);
FuzzySet *ST = new FuzzySet(9.5, 10, 14, 14);

//INPUT FUZZY AMONIA
FuzzySet *SK = new FuzzySet( 0, 0, 0.015, 0.017);
FuzzySet *K = new FuzzySet( 0.015, 0.016, 0.019, 0.020);
FuzzySet *C = new FuzzySet(0.019, 0.020, 0.025, 0.026);
FuzzySet *Br = new FuzzySet(0.024, 0.026, 0.034, 0.035);
FuzzySet *SBR = new FuzzySet(0.034, 0.035, 0.100, 0.100);

//INPUT FUZZY SUHU
FuzzySet *Sd = new FuzzySet( 15, 15, 20, 21);
FuzzySet *D = new FuzzySet(20, 21, 24, 25);
FuzzySet *N = new FuzzySet(24, 25, 31, 32);
FuzzySet *P = new FuzzySet(31, 32, 34, 36);
FuzzySet *SP = new FuzzySet(35, 36, 40, 40);

//OUTPUT FUZZY
FuzzySet *SA = new FuzzySet(0, 0, 14, 15);
FuzzySet *A = new FuzzySet( 14, 15, 29, 30);
FuzzySet *S = new FuzzySet( 29, 30, 49, 50);
FuzzySet *BK = new FuzzySet(49, 50, 69, 70);
FuzzySet *SB = new FuzzySet(69, 70, 100, 100);

// function kustom get data harian hapus data lama
String getTodayDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";

  char buffer[11];
  sprintf(buffer, "%04d-%02d-%02d", 
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday);

  return String(buffer);
}

bool isNineAM() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  return timeinfo.tm_hour == 22 && timeinfo.tm_min == 55;
}

bool getTime(struct tm &timeinfo) {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Gagal mendapatkan waktu lokal!");
    return false;
  }
  return true;
}

void cleanupOldData() {
  FirebaseJson json;

  if (!Firebase.RTDB.getJSON(&fbdo, "/chart/harian", &json)) {
    Serial.println("Gagal ambil JSON");
    return;
  }

  size_t count = json.iteratorBegin();

  if (count > 15) {
    int type;
    String key, value;

    // Ambil key pertama (data terlambat)
    json.iteratorGet(0, type, key, value);

    String pathHapus = "/chart/harian/" + key;
    Firebase.RTDB.deleteNode(&fbdo, pathHapus);
    
    Serial.print("Menghapus data lama: ");
    Serial.println(pathHapus);
  }

  json.iteratorEnd();
}


//============================================================

void setup() {
  // =====Koneksi ke WiFi=====
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nConnected to WiFi!");
//----------------------------------------------------------------------------------------------------------------------
// START WAKTU
 configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

configTime(utcOffsetInSeconds, 0, "pool.ntp.org", "time.nist.gov");

Serial.println("Mengambil waktu NTP...");
struct tm timeinfo;
while (!getLocalTime(&timeinfo)) {
    Serial.println("Gagal sync NTP, coba lagi...");
    delay(500);
}
Serial.println("Waktu NTP berhasil tersinkronisasi!");

//=====koneksi firebase=====
   /* Assign the api key (required) */
  //config.api_key = API_KEY; 

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;
  /* Sign up */
  //if (Firebase.signUp(&config, &auth, "", "")){
  //  Serial.println("ok");
  //  signupOK = true;
  //}
  //else{
  //  Serial.printf("%s\n", config.signer.signupError.message.c_str());
  //}

  
  Firebase.reconnectNetwork(true);
  config.token_status_callback = tokenStatusCallback; 
  
  //Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, nullptr);
  //-------------------------------------------------------------------------------------------------------------------

  // =====Inisialisasi LCD dan pin=====
  lcd.init();
  lcd.begin(16, 2);
  lcd.backlight();
  Serial.begin(115200);
  pinMode(ph_Pin, INPUT);
  pinMode(mq135Pin, INPUT);
  // Inisialisasi sensor suhu
  sensors.begin();
  //-------------------------------------------------------------------------------------------------------------------

  //setup fuzzy PH
  FuzzyInput *ph = new FuzzyInput(1);

  ph->addFuzzySet(SR);
  ph->addFuzzySet(R);
  ph->addFuzzySet(B);
  ph->addFuzzySet(T);
  ph->addFuzzySet(ST);
  fuzzy->addFuzzyInput(ph);

  //setup fuzzy amonia
  FuzzyInput *amonia = new FuzzyInput(2);

  amonia->addFuzzySet(SK);
  amonia->addFuzzySet(K);
  amonia->addFuzzySet(C);
  amonia->addFuzzySet(Br);
  amonia->addFuzzySet(SBR);
  fuzzy->addFuzzyInput(amonia);

  //setup fuzzy suhu
  FuzzyInput *suhu = new FuzzyInput(3);

  suhu->addFuzzySet(Sd);
  suhu->addFuzzySet(D);
  suhu->addFuzzySet(N);
  suhu->addFuzzySet(P);
  suhu->addFuzzySet(SP);
  fuzzy->addFuzzyInput(suhu);

  //setup fuzzy output
  FuzzyOutput *output = new FuzzyOutput(1);

  output->addFuzzySet(SA);
  output->addFuzzySet(A);
  output->addFuzzySet(S);
  output->addFuzzySet(BK);
  output->addFuzzySet(SB);
  fuzzy->addFuzzyOutput(output);
  //-----------------------------------------------------------------------------------------------------------------------------
//=====FUZZY RULE=====
//SUHU = SD D N P SP
//SANGAT DINGIN, DINGIN, NORMAL, PANAS, SANGAT PANAS
//==========================================================
//AMONIA = SK K C BR SBR
//SANGAT KECIL, KECIL, CUKUP, BESAR, SANGAT BESAR
//==========================================================
//PH = SR R B T ST 
// SANGAT RENDAH, RENDAH, BAIK, TINGGI, SANGAT TINGGI
//==========================================================
//KONDISI = SA A S BK SB
//SANGAT AMAN, AMAN, SEDANG, BURUK, SANGAT BURUK
//==========================================================

//1
FuzzyRuleAntecedent *sr_And_sk = new FuzzyRuleAntecedent();
sr_And_sk->joinWithAND(SR, SK);
FuzzyRuleAntecedent *ifsr_And_sk_And_sd = new FuzzyRuleAntecedent();
ifsr_And_sk_And_sd->joinWithAND(sr_And_sk, Sd);

FuzzyRuleConsequent *thenSB1 = new FuzzyRuleConsequent();
thenSB1->addOutput(SB);

FuzzyRule *fuzzyRule1 = new FuzzyRule(1, ifsr_And_sk_And_sd, thenSB1);
fuzzy->addFuzzyRule(fuzzyRule1);

//2
FuzzyRuleAntecedent *r_And_sk = new FuzzyRuleAntecedent();
r_And_sk->joinWithAND(R, SK);
FuzzyRuleAntecedent *ifr_And_sk_And_sd = new FuzzyRuleAntecedent();
ifr_And_sk_And_sd->joinWithAND(r_And_sk, Sd);

FuzzyRuleConsequent *thenBK1 = new FuzzyRuleConsequent();
thenBK1->addOutput(BK);

FuzzyRule *fuzzyRule2 = new FuzzyRule(2, ifr_And_sk_And_sd, thenBK1);
fuzzy->addFuzzyRule(fuzzyRule2);

//3
FuzzyRuleAntecedent *b_And_sk = new FuzzyRuleAntecedent();
b_And_sk->joinWithAND(B, SK);
FuzzyRuleAntecedent *ifb_And_sk_And_sd = new FuzzyRuleAntecedent();
ifb_And_sk_And_sd->joinWithAND(b_And_sk, Sd);

FuzzyRuleConsequent *thenS1 = new FuzzyRuleConsequent();
thenS1->addOutput(S);

FuzzyRule *fuzzyRule3 = new FuzzyRule(3, ifb_And_sk_And_sd, thenS1);
fuzzy->addFuzzyRule(fuzzyRule3);

//4
FuzzyRuleAntecedent *t_And_sk = new FuzzyRuleAntecedent();
t_And_sk->joinWithAND(T, SK);
FuzzyRuleAntecedent *ift_And_sk_And_sd = new FuzzyRuleAntecedent();
ift_And_sk_And_sd->joinWithAND(t_And_sk, Sd);

FuzzyRuleConsequent *thenBK2 = new FuzzyRuleConsequent();
thenBK2->addOutput(BK);

FuzzyRule *fuzzyRule4 = new FuzzyRule(4, ift_And_sk_And_sd, thenBK2);
fuzzy->addFuzzyRule(fuzzyRule4);

//5
FuzzyRuleAntecedent *st_And_sk = new FuzzyRuleAntecedent();
st_And_sk->joinWithAND(ST, SK);
FuzzyRuleAntecedent *ifst_And_sk_And_sd = new FuzzyRuleAntecedent();
ifst_And_sk_And_sd->joinWithAND(st_And_sk, Sd);

FuzzyRuleConsequent *thenSB2 = new FuzzyRuleConsequent();
thenSB2->addOutput(SB);

FuzzyRule *fuzzyRule5 = new FuzzyRule(5, ifst_And_sk_And_sd, thenSB2);
fuzzy->addFuzzyRule(fuzzyRule5);

//6
FuzzyRuleAntecedent *sr_And_k1 = new FuzzyRuleAntecedent();
sr_And_k1->joinWithAND(SR, K);
FuzzyRuleAntecedent *ifsr_And_k_And_sd1 = new FuzzyRuleAntecedent();
ifsr_And_k_And_sd1->joinWithAND(sr_And_k1, Sd);

FuzzyRuleConsequent *thenSB3 = new FuzzyRuleConsequent();
thenSB3->addOutput(SB);

FuzzyRule *fuzzyRule6 = new FuzzyRule(6, ifsr_And_k_And_sd1, thenSB3);
fuzzy->addFuzzyRule(fuzzyRule6);

//7
FuzzyRuleAntecedent *r_And_k1 = new FuzzyRuleAntecedent();
r_And_k1->joinWithAND(R, K);
FuzzyRuleAntecedent *ifr_And_k_And_sd1 = new FuzzyRuleAntecedent();
ifr_And_k_And_sd1->joinWithAND(r_And_k1, Sd);

FuzzyRuleConsequent *thenBK3 = new FuzzyRuleConsequent();
thenBK3->addOutput(BK);

FuzzyRule *fuzzyRule7 = new FuzzyRule(7, ifr_And_k_And_sd1, thenBK3);
fuzzy->addFuzzyRule(fuzzyRule7);

//8
FuzzyRuleAntecedent *b_And_k1 = new FuzzyRuleAntecedent();
b_And_k1->joinWithAND(SR, K);
FuzzyRuleAntecedent *ifb_And_k_And_sd1 = new FuzzyRuleAntecedent();
ifb_And_k_And_sd1->joinWithAND(b_And_k1, Sd);

FuzzyRuleConsequent *thenS2 = new FuzzyRuleConsequent();
thenS2->addOutput(S);

FuzzyRule *fuzzyRule8 = new FuzzyRule(8, ifb_And_k_And_sd1, thenS2);
fuzzy->addFuzzyRule(fuzzyRule8);

//9
FuzzyRuleAntecedent *t_And_k1 = new FuzzyRuleAntecedent();
t_And_k1->joinWithAND(T, K);
FuzzyRuleAntecedent *ift_And_k_And_sd1 = new FuzzyRuleAntecedent();
ift_And_k_And_sd1->joinWithAND(t_And_k1, S);

FuzzyRuleConsequent *thenBK4 = new FuzzyRuleConsequent();
thenBK4->addOutput(BK);

FuzzyRule *fuzzyRule9 = new FuzzyRule(9, ift_And_k_And_sd1, thenBK4);
fuzzy->addFuzzyRule(fuzzyRule9);

//10
FuzzyRuleAntecedent *st_And_k1 = new FuzzyRuleAntecedent();
st_And_k1->joinWithAND(ST, K);
FuzzyRuleAntecedent *ifst_And_k_And_sd1 = new FuzzyRuleAntecedent();
ift_And_k_And_sd1->joinWithAND(st_And_k1, S);

FuzzyRuleConsequent *thenSB4 = new FuzzyRuleConsequent();
thenSB4->addOutput(SB);

FuzzyRule *fuzzyRule10 = new FuzzyRule(10, ifst_And_k_And_sd1, thenSB4);
fuzzy->addFuzzyRule(fuzzyRule10);

//11
FuzzyRuleAntecedent *sr_And_c2 = new FuzzyRuleAntecedent();
sr_And_c2->joinWithAND(SR, C);
FuzzyRuleAntecedent *ifsr_And_c_And_sd2 = new FuzzyRuleAntecedent();
ifsr_And_c_And_sd2->joinWithAND(sr_And_c2, Sd);

FuzzyRuleConsequent *thenSB5 = new FuzzyRuleConsequent();
thenSB5->addOutput(SB);

FuzzyRule *fuzzyRule11 = new FuzzyRule(11, ifsr_And_c_And_sd2, thenSB5);
fuzzy->addFuzzyRule(fuzzyRule11);

//12
FuzzyRuleAntecedent *r_And_c2 = new FuzzyRuleAntecedent();
r_And_c2->joinWithAND(R, C);
FuzzyRuleAntecedent *ifr_And_c_And_sd2 = new FuzzyRuleAntecedent();
ifr_And_c_And_sd2->joinWithAND(r_And_c2, Sd);

FuzzyRuleConsequent *thenBK5 = new FuzzyRuleConsequent();
thenBK5->addOutput(BK);

FuzzyRule *fuzzyRule12 = new FuzzyRule(12, ifr_And_c_And_sd2, thenBK5);
fuzzy->addFuzzyRule(fuzzyRule12);

//13
FuzzyRuleAntecedent *b_And_c2 = new FuzzyRuleAntecedent();
b_And_c2->joinWithAND(B, C);
FuzzyRuleAntecedent *ifb_And_c_And_sd2 = new FuzzyRuleAntecedent();
ifb_And_c_And_sd2->joinWithAND(b_And_c2, Sd);

FuzzyRuleConsequent *thenS3 = new FuzzyRuleConsequent();
thenS3->addOutput(S);

FuzzyRule *fuzzyRule13 = new FuzzyRule(13, ifb_And_c_And_sd2, thenS3);
fuzzy->addFuzzyRule(fuzzyRule13);

//14
FuzzyRuleAntecedent *t_And_c2 = new FuzzyRuleAntecedent();
t_And_c2->joinWithAND(T, C);
FuzzyRuleAntecedent *ift_And_c_And_sd2 = new FuzzyRuleAntecedent();
ift_And_c_And_sd2->joinWithAND(t_And_c2, Sd);

FuzzyRuleConsequent *thenBK6 = new FuzzyRuleConsequent();
thenBK6->addOutput(BK);

FuzzyRule *fuzzyRule14 = new FuzzyRule(14, ift_And_c_And_sd2, thenBK6);
fuzzy->addFuzzyRule(fuzzyRule14);

//15
FuzzyRuleAntecedent *st_And_c2 = new FuzzyRuleAntecedent();
st_And_c2->joinWithAND(ST, C);
FuzzyRuleAntecedent *ifst_And_c_And_sd2 = new FuzzyRuleAntecedent();
ifst_And_c_And_sd2->joinWithAND(st_And_c2, Sd);

FuzzyRuleConsequent *thenSB6 = new FuzzyRuleConsequent();
thenSB6->addOutput(SB);

FuzzyRule *fuzzyRule15 = new FuzzyRule(15, ifst_And_c_And_sd2, thenSB6);
fuzzy->addFuzzyRule(fuzzyRule15);

//16
FuzzyRuleAntecedent *sr_And_br3 = new FuzzyRuleAntecedent();
sr_And_br3->joinWithAND(SR, Br);
FuzzyRuleAntecedent *ifsr_And_br_And_sd3 = new FuzzyRuleAntecedent();
ifsr_And_br_And_sd3->joinWithAND(sr_And_br3, Sd);

FuzzyRuleConsequent *thenSBN = new FuzzyRuleConsequent();
thenSBN->addOutput(SB);

FuzzyRule *fuzzyRule16 = new FuzzyRule(16, ifsr_And_br_And_sd3, thenSBN);
fuzzy->addFuzzyRule(fuzzyRule16);

//17
FuzzyRuleAntecedent *r_And_br3 = new FuzzyRuleAntecedent();
r_And_br3->joinWithAND(R, Br);
FuzzyRuleAntecedent *ifr_And_br_And_sd3 = new FuzzyRuleAntecedent();
ifr_And_br_And_sd3->joinWithAND(r_And_br3, Sd);

FuzzyRuleConsequent *thenBK7 = new FuzzyRuleConsequent();
thenBK7->addOutput(BK);

FuzzyRule *fuzzyRule17 = new FuzzyRule(17, ifr_And_br_And_sd3, thenBK7);
fuzzy->addFuzzyRule(fuzzyRule17);

//18
FuzzyRuleAntecedent *b_And_br3 = new FuzzyRuleAntecedent();
b_And_br3->joinWithAND(SR, Br);
FuzzyRuleAntecedent *ifb_And_br_And_sd3 = new FuzzyRuleAntecedent();
ifb_And_br_And_sd3->joinWithAND(b_And_br3, Sd);

FuzzyRuleConsequent *thenBK8 = new FuzzyRuleConsequent();
thenBK8->addOutput(BK);

FuzzyRule *fuzzyRule18 = new FuzzyRule(18, ifb_And_br_And_sd3, thenBK8);
fuzzy->addFuzzyRule(fuzzyRule18);

//19
FuzzyRuleAntecedent *t_And_br3 = new FuzzyRuleAntecedent();
t_And_br3->joinWithAND(T, Br);
FuzzyRuleAntecedent *ift_And_br_And_sd3 = new FuzzyRuleAntecedent();
ift_And_br_And_sd3->joinWithAND(t_And_br3, Sd);

FuzzyRuleConsequent *thenBK9 = new FuzzyRuleConsequent();
thenBK9->addOutput(BK);

FuzzyRule *fuzzyRule19 = new FuzzyRule(19, ift_And_br_And_sd3, thenBK9);
fuzzy->addFuzzyRule(fuzzyRule19);

//20
FuzzyRuleAntecedent *st_And_br3 = new FuzzyRuleAntecedent();
st_And_br3->joinWithAND(ST, Br);
FuzzyRuleAntecedent *ifst_And_br_And_sd3 = new FuzzyRuleAntecedent();
ifst_And_br_And_sd3->joinWithAND(st_And_br3, Sd);

FuzzyRuleConsequent *thenSB7 = new FuzzyRuleConsequent();
thenSB7->addOutput(SB);

FuzzyRule *fuzzyRule20 = new FuzzyRule(20, ifst_And_br_And_sd3, thenSB7);
fuzzy->addFuzzyRule(fuzzyRule20);

//21
FuzzyRuleAntecedent *sr_And_sbr4 = new FuzzyRuleAntecedent();
sr_And_sbr4->joinWithAND(SR, SBR);
FuzzyRuleAntecedent *ifsr_And_sbr_And_sd4 = new FuzzyRuleAntecedent();
ifsr_And_sbr_And_sd4->joinWithAND(sr_And_sbr4, Sd);

FuzzyRuleConsequent *thenSB8 = new FuzzyRuleConsequent();
thenSB8->addOutput(SB);

FuzzyRule *fuzzyRule21 = new FuzzyRule(21, ifsr_And_sbr_And_sd4, thenSB8);
fuzzy->addFuzzyRule(fuzzyRule21);

//22
FuzzyRuleAntecedent *r_And_sbr4 = new FuzzyRuleAntecedent();
r_And_sbr4->joinWithAND(R, SBR);
FuzzyRuleAntecedent *ifr_And_sbr_And_sd4 = new FuzzyRuleAntecedent();
ifr_And_sbr_And_sd4->joinWithAND(r_And_sbr4, Sd);

FuzzyRuleConsequent *thenSB9 = new FuzzyRuleConsequent();
thenSB9->addOutput(SB);

FuzzyRule *fuzzyRule22 = new FuzzyRule(22, ifr_And_sbr_And_sd4, thenSB9);
fuzzy->addFuzzyRule(fuzzyRule22);

//23
FuzzyRuleAntecedent *b_And_sbr4 = new FuzzyRuleAntecedent();
b_And_sbr4->joinWithAND(B, SBR);
FuzzyRuleAntecedent *ifb_And_sbr_And_sd4 = new FuzzyRuleAntecedent();
ifb_And_sbr_And_sd4->joinWithAND(b_And_sbr4, Sd);

FuzzyRuleConsequent *thenSB10 = new FuzzyRuleConsequent();
thenSB10->addOutput(SB);

FuzzyRule *fuzzyRule23 = new FuzzyRule(23, ifb_And_sbr_And_sd4, thenSB10);
fuzzy->addFuzzyRule(fuzzyRule23);

//24
FuzzyRuleAntecedent *t_And_sbr4 = new FuzzyRuleAntecedent();
t_And_sbr4->joinWithAND(T, SBR);
FuzzyRuleAntecedent *ift_And_sbr_And_sd4 = new FuzzyRuleAntecedent();
ift_And_sbr_And_sd4->joinWithAND(t_And_sbr4, Sd);

FuzzyRuleConsequent *thenSB11 = new FuzzyRuleConsequent();
thenSB11->addOutput(SB);

FuzzyRule *fuzzyRule24 = new FuzzyRule(24, ift_And_sbr_And_sd4, thenSB11);
fuzzy->addFuzzyRule(fuzzyRule24);

//25
FuzzyRuleAntecedent *st_And_sbr4 = new FuzzyRuleAntecedent();
st_And_sbr4->joinWithAND(ST, SBR);
FuzzyRuleAntecedent *ifst_And_sbr_And_sd4 = new FuzzyRuleAntecedent();
ifst_And_sbr_And_sd4->joinWithAND(st_And_sbr4, Sd);

FuzzyRuleConsequent *thenSB12 = new FuzzyRuleConsequent();
thenSB12->addOutput(SB);

FuzzyRule *fuzzyRule25 = new FuzzyRule(25, ifst_And_sbr_And_sd4, thenSB12);
fuzzy->addFuzzyRule(fuzzyRule25);

//26
FuzzyRuleAntecedent *sr_And_sk5 = new FuzzyRuleAntecedent();
sr_And_sk5->joinWithAND(SR, SK);
FuzzyRuleAntecedent *ifsr_And_sk_And_d5 = new FuzzyRuleAntecedent();
ifsr_And_sk_And_d5->joinWithAND(sr_And_sk5, D);

FuzzyRuleConsequent *thenBK10 = new FuzzyRuleConsequent();
thenBK10->addOutput(BK);

FuzzyRule *fuzzyRule26 = new FuzzyRule(26, ifsr_And_sk_And_d5, thenBK10);
fuzzy->addFuzzyRule(fuzzyRule26);

//27
FuzzyRuleAntecedent *r_And_sk5 = new FuzzyRuleAntecedent();
r_And_sk5->joinWithAND(R, SK);
FuzzyRuleAntecedent *ifr_And_sk_And_d5 = new FuzzyRuleAntecedent();
ifr_And_sk_And_d5->joinWithAND(r_And_sk5, D);

FuzzyRuleConsequent *thenS4 = new FuzzyRuleConsequent();
thenS4->addOutput(S);

FuzzyRule *fuzzyRule27 = new FuzzyRule(27, ifr_And_sk_And_d5, thenS4);
fuzzy->addFuzzyRule(fuzzyRule27);

//28
FuzzyRuleAntecedent *b_And_sk5 = new FuzzyRuleAntecedent();
b_And_sk5->joinWithAND(B, SK);
FuzzyRuleAntecedent *ifb_And_sk_And_d5 = new FuzzyRuleAntecedent();
ifb_And_sk_And_d5->joinWithAND(b_And_sk5, D);

FuzzyRuleConsequent *thenA = new FuzzyRuleConsequent();
thenA->addOutput(A);

FuzzyRule *fuzzyRule28 = new FuzzyRule(28, ifb_And_sk_And_d5, thenA);
fuzzy->addFuzzyRule(fuzzyRule28);

//29
FuzzyRuleAntecedent *t_And_sk5 = new FuzzyRuleAntecedent();
t_And_sk5->joinWithAND(T, SK);
FuzzyRuleAntecedent *ift_And_sk_And_d5 = new FuzzyRuleAntecedent();
ift_And_sk_And_d5->joinWithAND(t_And_sk5, D);

FuzzyRuleConsequent *thenS5 = new FuzzyRuleConsequent();
thenS5->addOutput(S);

FuzzyRule *fuzzyRule29 = new FuzzyRule(29, ift_And_sk_And_d5, thenS5);
fuzzy->addFuzzyRule(fuzzyRule29);

//30
FuzzyRuleAntecedent *st_And_sk5 = new FuzzyRuleAntecedent();
st_And_sk5->joinWithAND(ST, SK);
FuzzyRuleAntecedent *ifst_And_sk_And_d5 = new FuzzyRuleAntecedent();
ifst_And_sk_And_d5->joinWithAND(st_And_sk5, D);

FuzzyRuleConsequent *thenBK11 = new FuzzyRuleConsequent();
thenBK10->addOutput(BK);

FuzzyRule *fuzzyRule30 = new FuzzyRule(30, ifst_And_sk_And_d5, thenBK11);
fuzzy->addFuzzyRule(fuzzyRule30);

//31
FuzzyRuleAntecedent *sr_And_k6 = new FuzzyRuleAntecedent();
sr_And_k6->joinWithAND(SR, K);
FuzzyRuleAntecedent *ifsr_And_k_And_d6 = new FuzzyRuleAntecedent();
ifsr_And_k_And_d6->joinWithAND(sr_And_k6, D);

FuzzyRuleConsequent *thenBK12 = new FuzzyRuleConsequent();
thenBK12->addOutput(BK);

FuzzyRule *fuzzyRule31 = new FuzzyRule(31, ifsr_And_k_And_d6, thenBK12);
fuzzy->addFuzzyRule(fuzzyRule31);

//32
FuzzyRuleAntecedent *r_And_k6 = new FuzzyRuleAntecedent();
r_And_k6->joinWithAND(R, K);
FuzzyRuleAntecedent *ifr_And_k_And_d6 = new FuzzyRuleAntecedent();
ifr_And_k_And_d6->joinWithAND(r_And_k6, D);

FuzzyRuleConsequent *thenS6 = new FuzzyRuleConsequent();
thenS6->addOutput(S);

FuzzyRule *fuzzyRule32 = new FuzzyRule(32, ifr_And_k_And_d6, thenS6);
fuzzy->addFuzzyRule(fuzzyRule32);

//33
FuzzyRuleAntecedent *b_And_k6 = new FuzzyRuleAntecedent();
b_And_k6->joinWithAND(B, K);
FuzzyRuleAntecedent *ifb_And_k_And_d6 = new FuzzyRuleAntecedent();
ifb_And_k_And_d6->joinWithAND(b_And_k6, D);

FuzzyRuleConsequent *thenA1 = new FuzzyRuleConsequent();
thenA1->addOutput(A);

FuzzyRule *fuzzyRule33 = new FuzzyRule(33, ifb_And_k_And_d6, thenA1);
fuzzy->addFuzzyRule(fuzzyRule33);

//34
FuzzyRuleAntecedent *t_And_k6 = new FuzzyRuleAntecedent();
t_And_k6->joinWithAND(T, K);
FuzzyRuleAntecedent *ift_And_k_And_d6 = new FuzzyRuleAntecedent();
ift_And_k_And_d6->joinWithAND(t_And_k6, D);

FuzzyRuleConsequent *thenS7 = new FuzzyRuleConsequent();
thenS7->addOutput(S);

FuzzyRule *fuzzyRule34 = new FuzzyRule(34, ift_And_k_And_d6, thenS7);
fuzzy->addFuzzyRule(fuzzyRule34);

//35
FuzzyRuleAntecedent *st_And_k6 = new FuzzyRuleAntecedent();
st_And_k6->joinWithAND(ST, K);
FuzzyRuleAntecedent *ifst_And_k_And_d6 = new FuzzyRuleAntecedent();
ifst_And_k_And_d6->joinWithAND(st_And_k6, D);

FuzzyRuleConsequent *thenBK13 = new FuzzyRuleConsequent();
thenBK13->addOutput(BK);

FuzzyRule *fuzzyRule35 = new FuzzyRule(35, ifst_And_k_And_d6, thenBK13);
fuzzy->addFuzzyRule(fuzzyRule33);

//36
FuzzyRuleAntecedent *sr_And_c7 = new FuzzyRuleAntecedent();
sr_And_c7->joinWithAND(SR, C);
FuzzyRuleAntecedent *ifsr_And_c_And_d7 = new FuzzyRuleAntecedent();
ifsr_And_c_And_d7->joinWithAND(sr_And_c7, D);

FuzzyRuleConsequent *thenBK14 = new FuzzyRuleConsequent();
thenBK14->addOutput(BK);

FuzzyRule *fuzzyRule36 = new FuzzyRule(36, ifsr_And_c_And_d7, thenBK14);
fuzzy->addFuzzyRule(fuzzyRule36);

//37
FuzzyRuleAntecedent *r_And_c7 = new FuzzyRuleAntecedent();
r_And_c7->joinWithAND(R, C);
FuzzyRuleAntecedent *ifr_And_c_And_d7 = new FuzzyRuleAntecedent();
ifr_And_c_And_d7->joinWithAND(r_And_c7, D);

FuzzyRuleConsequent *thenS8 = new FuzzyRuleConsequent();
thenS8->addOutput(S);

FuzzyRule *fuzzyRule37 = new FuzzyRule(37, ifr_And_c_And_d7, thenS8);
fuzzy->addFuzzyRule(fuzzyRule37);

//38
FuzzyRuleAntecedent *b_And_c7 = new FuzzyRuleAntecedent();
b_And_c7->joinWithAND(B, C);
FuzzyRuleAntecedent *ifb_And_c_And_d7 = new FuzzyRuleAntecedent();
ifb_And_c_And_d7->joinWithAND(b_And_c7, D);

FuzzyRuleConsequent *thenA2 = new FuzzyRuleConsequent();
thenA2->addOutput(A);

FuzzyRule *fuzzyRule38 = new FuzzyRule(38, ifb_And_c_And_d7, thenA2);
fuzzy->addFuzzyRule(fuzzyRule38);

//39
FuzzyRuleAntecedent *t_And_c7 = new FuzzyRuleAntecedent();
t_And_c7->joinWithAND(T, C);
FuzzyRuleAntecedent *ift_And_c_And_d7 = new FuzzyRuleAntecedent();
ift_And_c_And_d7->joinWithAND(t_And_c7, D);

FuzzyRuleConsequent *thenS9 = new FuzzyRuleConsequent();
thenS9->addOutput(S);

FuzzyRule *fuzzyRule39 = new FuzzyRule(39, ift_And_c_And_d7, thenS9);
fuzzy->addFuzzyRule(fuzzyRule39);

//40
FuzzyRuleAntecedent *st_And_c7 = new FuzzyRuleAntecedent();
st_And_c7->joinWithAND(ST, C);
FuzzyRuleAntecedent *ifst_And_c_And_d7 = new FuzzyRuleAntecedent();
ifst_And_c_And_d7->joinWithAND(st_And_c7, D);

FuzzyRuleConsequent *thenBK15 = new FuzzyRuleConsequent();
thenBK15->addOutput(BK);

FuzzyRule *fuzzyRule40 = new FuzzyRule(40, ifst_And_c_And_d7, thenBK15);
fuzzy->addFuzzyRule(fuzzyRule40);

//41
FuzzyRuleAntecedent *sr_And_br8 = new FuzzyRuleAntecedent();
sr_And_br8->joinWithAND(SR, Br);
FuzzyRuleAntecedent *ifsr_And_br_And_d8 = new FuzzyRuleAntecedent();
ifsr_And_br_And_d8->joinWithAND(sr_And_br8, D);

FuzzyRuleConsequent *thenBK16 = new FuzzyRuleConsequent();
thenBK16->addOutput(BK);

FuzzyRule *fuzzyRule41 = new FuzzyRule(41, ifsr_And_br_And_d8, thenBK16);
fuzzy->addFuzzyRule(fuzzyRule41);

//42
FuzzyRuleAntecedent *r_And_br8 = new FuzzyRuleAntecedent();
r_And_br8->joinWithAND(R, Br);
FuzzyRuleAntecedent *ifr_And_br_And_d8 = new FuzzyRuleAntecedent();
ifr_And_br_And_d8->joinWithAND(r_And_br8, D);

FuzzyRuleConsequent *thenBK17 = new FuzzyRuleConsequent();
thenBK17->addOutput(BK);

FuzzyRule *fuzzyRule42 = new FuzzyRule(42, ifr_And_br_And_d8, thenBK17);
fuzzy->addFuzzyRule(fuzzyRule42);

//43
FuzzyRuleAntecedent *b_And_br8 = new FuzzyRuleAntecedent();
b_And_br8->joinWithAND(B,Br);
FuzzyRuleAntecedent *ifb_And_br_And_d8 = new FuzzyRuleAntecedent();
ifb_And_br_And_d8->joinWithAND(b_And_br8, D);

FuzzyRuleConsequent *thenBK18 = new FuzzyRuleConsequent();
thenBK18->addOutput(BK);

FuzzyRule *fuzzyRule43 = new FuzzyRule(41, ifsr_And_br_And_d8, thenBK18);
fuzzy->addFuzzyRule(fuzzyRule43);

//44
FuzzyRuleAntecedent *t_And_br8 = new FuzzyRuleAntecedent();
t_And_br8->joinWithAND(T, Br);
FuzzyRuleAntecedent *ift_And_br_And_d8 = new FuzzyRuleAntecedent();
ift_And_br_And_d8->joinWithAND(t_And_br8, D);

FuzzyRuleConsequent *thenBK19 = new FuzzyRuleConsequent();
thenBK19->addOutput(BK);

FuzzyRule *fuzzyRule44 = new FuzzyRule(44, ift_And_br_And_d8, thenBK19);
fuzzy->addFuzzyRule(fuzzyRule44);

//45
FuzzyRuleAntecedent *st_And_br8 = new FuzzyRuleAntecedent();
st_And_br8->joinWithAND(ST, Br);
FuzzyRuleAntecedent *ifst_And_br_And_d8 = new FuzzyRuleAntecedent();
ifst_And_br_And_d8->joinWithAND(st_And_br8, D);

FuzzyRuleConsequent *thenBK20 = new FuzzyRuleConsequent();
thenBK20->addOutput(BK);

FuzzyRule *fuzzyRule45 = new FuzzyRule(45, ifst_And_br_And_d8, thenBK20);
fuzzy->addFuzzyRule(fuzzyRule45);

//46
FuzzyRuleAntecedent *sr_And_sbr9 = new FuzzyRuleAntecedent();
sr_And_sbr9->joinWithAND(SR, SBR);
FuzzyRuleAntecedent *ifsr_And_sbr_And_d9 = new FuzzyRuleAntecedent();
ifsr_And_sbr_And_d9->joinWithAND(sr_And_sbr9, D);

FuzzyRuleConsequent *thenSB13 = new FuzzyRuleConsequent();
thenSB13->addOutput(SB);

FuzzyRule *fuzzyRule46 = new FuzzyRule(46, ifsr_And_sbr_And_d9, thenSB13);
fuzzy->addFuzzyRule(fuzzyRule46);

//47
FuzzyRuleAntecedent *r_And_sbr9 = new FuzzyRuleAntecedent();
r_And_sbr9->joinWithAND(R, SBR);
FuzzyRuleAntecedent *ifr_And_sbr_And_d9 = new FuzzyRuleAntecedent();
ifr_And_sbr_And_d9->joinWithAND(r_And_sbr9, D);

FuzzyRuleConsequent *thenBK21 = new FuzzyRuleConsequent();
thenBK21->addOutput(BK);

FuzzyRule *fuzzyRule47 = new FuzzyRule(47, ifr_And_sbr_And_d9, thenBK21);
fuzzy->addFuzzyRule(fuzzyRule47);

//48
FuzzyRuleAntecedent *b_And_sbr9 = new FuzzyRuleAntecedent();
b_And_sbr9->joinWithAND(B, SBR);
FuzzyRuleAntecedent *ifb_And_sbr_And_d9 = new FuzzyRuleAntecedent();
ifb_And_sbr_And_d9->joinWithAND(b_And_sbr9, D);

FuzzyRuleConsequent *thenBK22 = new FuzzyRuleConsequent();
thenBK22->addOutput(BK);

FuzzyRule *fuzzyRule48 = new FuzzyRule(48, ifb_And_sbr_And_d9, thenBK22);
fuzzy->addFuzzyRule(fuzzyRule48);

//49
FuzzyRuleAntecedent *t_And_sbr9 = new FuzzyRuleAntecedent();
t_And_sbr9->joinWithAND(T, SBR);
FuzzyRuleAntecedent *ift_And_sbr_And_d9 = new FuzzyRuleAntecedent();
ift_And_sbr_And_d9->joinWithAND(t_And_sbr9, D);

FuzzyRuleConsequent *thenBK23 = new FuzzyRuleConsequent();
thenBK23->addOutput(BK);

FuzzyRule *fuzzyRule49 = new FuzzyRule(49, ift_And_sbr_And_d9, thenBK23);
fuzzy->addFuzzyRule(fuzzyRule49);

//50
FuzzyRuleAntecedent *st_And_sbr9 = new FuzzyRuleAntecedent();
st_And_sbr9->joinWithAND(ST, SBR);
FuzzyRuleAntecedent *ifst_And_sbr_And_d9 = new FuzzyRuleAntecedent();
ifst_And_sbr_And_d9->joinWithAND(st_And_sbr9, D);

FuzzyRuleConsequent *thenSB14 = new FuzzyRuleConsequent();
thenSB14->addOutput(SB);

FuzzyRule *fuzzyRule50 = new FuzzyRule(50, ifst_And_sbr_And_d9, thenSB14);
fuzzy->addFuzzyRule(fuzzyRule50);

//51
FuzzyRuleAntecedent *sr_And_sk10 = new FuzzyRuleAntecedent();
sr_And_sk10->joinWithAND(SR, SK);
FuzzyRuleAntecedent *ifsr_And_sk_And_n10 = new FuzzyRuleAntecedent();
ifsr_And_sk_And_n10->joinWithAND(sr_And_sk10, N);

FuzzyRuleConsequent *thenBK24 = new FuzzyRuleConsequent();
thenBK24->addOutput(BK);

FuzzyRule *fuzzyRule51 = new FuzzyRule(51, ifsr_And_sk_And_n10, thenBK24);
fuzzy->addFuzzyRule(fuzzyRule51);

//52
FuzzyRuleAntecedent *r_And_sk10 = new FuzzyRuleAntecedent();
r_And_sk10->joinWithAND(R, SK);
FuzzyRuleAntecedent *ifr_And_sk_And_n10 = new FuzzyRuleAntecedent();
ifr_And_sk_And_n10->joinWithAND(r_And_sk10, N);

FuzzyRuleConsequent *thenA3 = new FuzzyRuleConsequent();
thenA3->addOutput(A);

FuzzyRule *fuzzyRule52 = new FuzzyRule(52, ifr_And_sk_And_n10, thenA3);
fuzzy->addFuzzyRule(fuzzyRule52);

//53
FuzzyRuleAntecedent *b_And_sk10 = new FuzzyRuleAntecedent();
b_And_sk10->joinWithAND(B, SK);
FuzzyRuleAntecedent *ifb_And_sk_And_n10 = new FuzzyRuleAntecedent();
ifb_And_sk_And_n10->joinWithAND(b_And_sk10, N);

FuzzyRuleConsequent *thenSA = new FuzzyRuleConsequent();
thenSA->addOutput(SA);

FuzzyRule *fuzzyRule53 = new FuzzyRule(53, ifb_And_sk_And_n10, thenSA);
fuzzy->addFuzzyRule(fuzzyRule53);

//54
FuzzyRuleAntecedent *t_And_sk10 = new FuzzyRuleAntecedent();
t_And_sk10->joinWithAND(T, SK);
FuzzyRuleAntecedent *ift_And_sk_And_n10 = new FuzzyRuleAntecedent();
ift_And_sk_And_n10->joinWithAND(t_And_sk10, N);

FuzzyRuleConsequent *thenA4 = new FuzzyRuleConsequent();
thenA4->addOutput(A);

FuzzyRule *fuzzyRule54 = new FuzzyRule(54, ift_And_sk_And_n10, thenA4);
fuzzy->addFuzzyRule(fuzzyRule54);

//55
FuzzyRuleAntecedent *st_And_sk10 = new FuzzyRuleAntecedent();
sr_And_sk10->joinWithAND(ST, SK);
FuzzyRuleAntecedent *ifst_And_sk_And_n10 = new FuzzyRuleAntecedent();
ifst_And_sk_And_n10->joinWithAND(st_And_sk10, N);

FuzzyRuleConsequent *thenBK25 = new FuzzyRuleConsequent();
thenBK25->addOutput(BK);

FuzzyRule *fuzzyRule55 = new FuzzyRule(55, ifst_And_sk_And_n10, thenBK25);
fuzzy->addFuzzyRule(fuzzyRule55);

//56
FuzzyRuleAntecedent *sr_And_k11 = new FuzzyRuleAntecedent();
sr_And_k11->joinWithAND(SR, K);
FuzzyRuleAntecedent *ifsr_And_k_And_n11 = new FuzzyRuleAntecedent();
ifsr_And_k_And_n11->joinWithAND(sr_And_k11, N);

FuzzyRuleConsequent *thenBK26 = new FuzzyRuleConsequent();
thenBK26->addOutput(BK);

FuzzyRule *fuzzyRule56 = new FuzzyRule(56, ifsr_And_k_And_n11, thenBK26);
fuzzy->addFuzzyRule(fuzzyRule56);

//57
FuzzyRuleAntecedent *r_And_k11 = new FuzzyRuleAntecedent();
r_And_k11->joinWithAND(R, K);
FuzzyRuleAntecedent *ifr_And_k_And_n11 = new FuzzyRuleAntecedent();
ifr_And_k_And_n11->joinWithAND(r_And_k11, N);

FuzzyRuleConsequent *thenA5 = new FuzzyRuleConsequent();
thenA5->addOutput(A);

FuzzyRule *fuzzyRule57 = new FuzzyRule(57, ifr_And_k_And_n11, thenA5);
fuzzy->addFuzzyRule(fuzzyRule57);

//58
FuzzyRuleAntecedent *b_And_k11 = new FuzzyRuleAntecedent();
b_And_k11->joinWithAND(B, K);
FuzzyRuleAntecedent *ifb_And_k_And_n11 = new FuzzyRuleAntecedent();
ifb_And_k_And_n11->joinWithAND(b_And_k11, N);

FuzzyRuleConsequent *thenSA1 = new FuzzyRuleConsequent();
thenSA1->addOutput(SA);

FuzzyRule *fuzzyRule58 = new FuzzyRule(58, ifb_And_k_And_n11, thenSA1);
fuzzy->addFuzzyRule(fuzzyRule58);

//59
FuzzyRuleAntecedent *t_And_k11 = new FuzzyRuleAntecedent();
t_And_k11->joinWithAND(T, K);
FuzzyRuleAntecedent *ift_And_k_And_n11 = new FuzzyRuleAntecedent();
ift_And_k_And_n11->joinWithAND(t_And_k11, N);

FuzzyRuleConsequent *thenA6 = new FuzzyRuleConsequent();
thenA6->addOutput(A);

FuzzyRule *fuzzyRule59 = new FuzzyRule(59, ift_And_k_And_n11, thenA6);
fuzzy->addFuzzyRule(fuzzyRule59);

//60
FuzzyRuleAntecedent *st_And_k11 = new FuzzyRuleAntecedent();
st_And_k11->joinWithAND(ST, K);
FuzzyRuleAntecedent *ifst_And_k_And_n11 = new FuzzyRuleAntecedent();
ifst_And_k_And_n11->joinWithAND(st_And_k11, N);

FuzzyRuleConsequent *thenBK27 = new FuzzyRuleConsequent();
thenBK27->addOutput(BK);

FuzzyRule *fuzzyRule60 = new FuzzyRule(60, ifsr_And_k_And_n11, thenBK27);
fuzzy->addFuzzyRule(fuzzyRule60);

//61
FuzzyRuleAntecedent *sr_And_c12 = new FuzzyRuleAntecedent();
sr_And_c12->joinWithAND(SR, C);
FuzzyRuleAntecedent *ifsr_And_c_And_n12 = new FuzzyRuleAntecedent();
ifsr_And_c_And_n12->joinWithAND(sr_And_c12, N);

FuzzyRuleConsequent *thenBK28 = new FuzzyRuleConsequent();
thenBK28->addOutput(BK);

FuzzyRule *fuzzyRule61 = new FuzzyRule(61, ifsr_And_c_And_n12, thenBK28);
fuzzy->addFuzzyRule(fuzzyRule61);

//62
FuzzyRuleAntecedent *r_And_c12 = new FuzzyRuleAntecedent();
r_And_c12->joinWithAND(R, C);
FuzzyRuleAntecedent *ifr_And_c_And_n12 = new FuzzyRuleAntecedent();
ifr_And_c_And_n12->joinWithAND(r_And_c12, N);

FuzzyRuleConsequent *thenA7 = new FuzzyRuleConsequent();
thenA7->addOutput(A);

FuzzyRule *fuzzyRule62 = new FuzzyRule(62, ifr_And_c_And_n12, thenA7);
fuzzy->addFuzzyRule(fuzzyRule62);

//63
FuzzyRuleAntecedent *b_And_c12 = new FuzzyRuleAntecedent();
b_And_c12->joinWithAND(B, C);
FuzzyRuleAntecedent *ifb_And_c_And_n12 = new FuzzyRuleAntecedent();
ifb_And_c_And_n12->joinWithAND(b_And_c12, N);

FuzzyRuleConsequent *thenSA2 = new FuzzyRuleConsequent();
thenSA2->addOutput(SA);

FuzzyRule *fuzzyRule63 = new FuzzyRule(63, ifb_And_c_And_n12, thenSA2);
fuzzy->addFuzzyRule(fuzzyRule63);

//64
FuzzyRuleAntecedent *t_And_c12 = new FuzzyRuleAntecedent();
t_And_c12->joinWithAND(T, C);
FuzzyRuleAntecedent *ift_And_c_And_n12 = new FuzzyRuleAntecedent();
ift_And_c_And_n12->joinWithAND(t_And_c12, N);

FuzzyRuleConsequent *thenA8 = new FuzzyRuleConsequent();
thenA8->addOutput(A);

FuzzyRule *fuzzyRule64 = new FuzzyRule(64, ift_And_c_And_n12, thenA8);
fuzzy->addFuzzyRule(fuzzyRule64);

//65
FuzzyRuleAntecedent *st_And_c12 = new FuzzyRuleAntecedent();
st_And_c12->joinWithAND(ST, C);
FuzzyRuleAntecedent *ifst_And_c_And_n12 = new FuzzyRuleAntecedent();
ifst_And_c_And_n12->joinWithAND(st_And_c12, N);

FuzzyRuleConsequent *thenBK29 = new FuzzyRuleConsequent();
thenBK29->addOutput(BK);

FuzzyRule *fuzzyRule65 = new FuzzyRule(65, ifst_And_c_And_n12, thenBK29);
fuzzy->addFuzzyRule(fuzzyRule65);

//66
FuzzyRuleAntecedent *sr_And_br13 = new FuzzyRuleAntecedent();
sr_And_br13->joinWithAND(SR, Br);
FuzzyRuleAntecedent *ifsr_And_br_And_n13 = new FuzzyRuleAntecedent();
ifsr_And_br_And_n13->joinWithAND(sr_And_br13, N);

FuzzyRuleConsequent *thenBK30 = new FuzzyRuleConsequent();
thenBK30->addOutput(BK);

FuzzyRule *fuzzyRule66 = new FuzzyRule(66, ifsr_And_br_And_n13, thenBK30);
fuzzy->addFuzzyRule(fuzzyRule66);

//67
FuzzyRuleAntecedent *r_And_br13 = new FuzzyRuleAntecedent();
r_And_br13->joinWithAND(R, Br);
FuzzyRuleAntecedent *ifr_And_br_And_n13 = new FuzzyRuleAntecedent();
ifr_And_br_And_n13->joinWithAND(r_And_br13, N);

FuzzyRuleConsequent *thenS10 = new FuzzyRuleConsequent();
thenS10->addOutput(S);

FuzzyRule *fuzzyRule67 = new FuzzyRule(67, ifsr_And_br_And_n13, thenS10);
fuzzy->addFuzzyRule(fuzzyRule67);

//68
FuzzyRuleAntecedent *b_And_br13 = new FuzzyRuleAntecedent();
b_And_br13->joinWithAND(B, Br);
FuzzyRuleAntecedent *ifb_And_br_And_n13 = new FuzzyRuleAntecedent();
ifb_And_br_And_n13->joinWithAND(b_And_br13, N);

FuzzyRuleConsequent *thenS11 = new FuzzyRuleConsequent();
thenS11->addOutput(S);

FuzzyRule *fuzzyRule68 = new FuzzyRule(68, ifb_And_br_And_n13, thenS11);
fuzzy->addFuzzyRule(fuzzyRule68);

//69
FuzzyRuleAntecedent *t_And_br13 = new FuzzyRuleAntecedent();
t_And_br13->joinWithAND(T, Br);
FuzzyRuleAntecedent *ift_And_br_And_n13 = new FuzzyRuleAntecedent();
ift_And_br_And_n13->joinWithAND(t_And_br13, N);

FuzzyRuleConsequent *thenS12 = new FuzzyRuleConsequent();
thenS12->addOutput(S);

FuzzyRule *fuzzyRule69 = new FuzzyRule(69, ift_And_br_And_n13, thenS12);
fuzzy->addFuzzyRule(fuzzyRule69);

//70
FuzzyRuleAntecedent *st_And_br13 = new FuzzyRuleAntecedent();
st_And_br13->joinWithAND(ST, Br);
FuzzyRuleAntecedent *ifst_And_br_And_n13 = new FuzzyRuleAntecedent();
ifst_And_br_And_n13->joinWithAND(st_And_br13, N);

FuzzyRuleConsequent *thenBK31 = new FuzzyRuleConsequent();
thenBK31->addOutput(BK);

FuzzyRule *fuzzyRule70 = new FuzzyRule(70, ifst_And_br_And_n13, thenBK31);
fuzzy->addFuzzyRule(fuzzyRule31);

//71
FuzzyRuleAntecedent *sr_And_sbr14 = new FuzzyRuleAntecedent();
sr_And_sbr14->joinWithAND(SR, SBR);
FuzzyRuleAntecedent *ifsr_And_sbr_And_n14 = new FuzzyRuleAntecedent();
ifsr_And_sbr_And_n14->joinWithAND(sr_And_sbr14, N);

FuzzyRuleConsequent *thenSB15 = new FuzzyRuleConsequent();
thenSB15->addOutput(SB);

FuzzyRule *fuzzyRule71 = new FuzzyRule(71, ifsr_And_sbr_And_n14, thenSB15);
fuzzy->addFuzzyRule(fuzzyRule71);

//72
FuzzyRuleAntecedent *r_And_sbr14 = new FuzzyRuleAntecedent();
r_And_sbr14->joinWithAND(R, SBR);
FuzzyRuleAntecedent *ifr_And_sbr_And_n14 = new FuzzyRuleAntecedent();
ifr_And_sbr_And_n14->joinWithAND(r_And_sbr14, N);

FuzzyRuleConsequent *thenBK32 = new FuzzyRuleConsequent();
thenBK32->addOutput(BK);

FuzzyRule *fuzzyRule72 = new FuzzyRule(72, ifr_And_sbr_And_n14, thenBK32);
fuzzy->addFuzzyRule(fuzzyRule72);

//73
FuzzyRuleAntecedent *b_And_sbr14 = new FuzzyRuleAntecedent();
b_And_sbr14->joinWithAND(B, SBR);
FuzzyRuleAntecedent *ifb_And_sbr_And_n14 = new FuzzyRuleAntecedent();
ifb_And_sbr_And_n14->joinWithAND(b_And_sbr14, N);

FuzzyRuleConsequent *thenBK33 = new FuzzyRuleConsequent();
thenBK33->addOutput(BK);

FuzzyRule *fuzzyRule73 = new FuzzyRule(73, ifb_And_sbr_And_n14, thenBK33);
fuzzy->addFuzzyRule(fuzzyRule73);

//74
FuzzyRuleAntecedent *t_And_sbr14 = new FuzzyRuleAntecedent();
t_And_sbr14->joinWithAND(T, SBR);
FuzzyRuleAntecedent *ift_And_sbr_And_n14 = new FuzzyRuleAntecedent();
ift_And_sbr_And_n14->joinWithAND(t_And_sbr14, N);

FuzzyRuleConsequent *thenBK34 = new FuzzyRuleConsequent();
thenBK34->addOutput(BK);

FuzzyRule *fuzzyRule74 = new FuzzyRule(74, ift_And_sbr_And_n14, thenBK34);
fuzzy->addFuzzyRule(fuzzyRule74);

//75
FuzzyRuleAntecedent *st_And_sbr14 = new FuzzyRuleAntecedent();
st_And_sbr14->joinWithAND(ST, SBR);
FuzzyRuleAntecedent *ifst_And_sbr_And_n14 = new FuzzyRuleAntecedent();
ifst_And_sbr_And_n14->joinWithAND(st_And_sbr14, N);

FuzzyRuleConsequent *thenSB16 = new FuzzyRuleConsequent();
thenSB16->addOutput(SB);

FuzzyRule *fuzzyRule75 = new FuzzyRule(75, ifst_And_sbr_And_n14, thenSB16);
fuzzy->addFuzzyRule(fuzzyRule75);

//76
FuzzyRuleAntecedent *sr_And_sk15 = new FuzzyRuleAntecedent();
sr_And_sk15->joinWithAND(SR, SK);
FuzzyRuleAntecedent *ifsr_And_sk_And_p15 = new FuzzyRuleAntecedent();
ifsr_And_sk_And_p15->joinWithAND(sr_And_sk15, P);

FuzzyRuleConsequent *thenBK35 = new FuzzyRuleConsequent();
thenBK35->addOutput(BK);

FuzzyRule *fuzzyRule76 = new FuzzyRule(76, ifsr_And_sk_And_p15, thenBK35);
fuzzy->addFuzzyRule(fuzzyRule76);

//77
FuzzyRuleAntecedent *r_And_sk15 = new FuzzyRuleAntecedent();
r_And_sk15->joinWithAND(R, SK);
FuzzyRuleAntecedent *ifr_And_sk_And_p15 = new FuzzyRuleAntecedent();
ifr_And_sk_And_p15->joinWithAND(r_And_sk15, P);

FuzzyRuleConsequent *thenS13 = new FuzzyRuleConsequent();
thenS13->addOutput(S);

FuzzyRule *fuzzyRule77 = new FuzzyRule(77, ifr_And_sk_And_p15, thenS13);
fuzzy->addFuzzyRule(fuzzyRule77);

//78
FuzzyRuleAntecedent *b_And_sk15 = new FuzzyRuleAntecedent();
b_And_sk15->joinWithAND(B, SK);
FuzzyRuleAntecedent *ifb_And_sk_And_p15 = new FuzzyRuleAntecedent();
ifb_And_sk_And_p15->joinWithAND(b_And_sk15, P);

FuzzyRuleConsequent *thenA9 = new FuzzyRuleConsequent();
thenA9->addOutput(A);

FuzzyRule *fuzzyRule78 = new FuzzyRule(78, ifb_And_sk_And_p15, thenA9);
fuzzy->addFuzzyRule(fuzzyRule78);

//79
FuzzyRuleAntecedent *t_And_sk15 = new FuzzyRuleAntecedent();
t_And_sk15->joinWithAND(T, SK);
FuzzyRuleAntecedent *ift_And_sk_And_p15 = new FuzzyRuleAntecedent();
ift_And_sk_And_p15->joinWithAND(t_And_sk15, P);

FuzzyRuleConsequent *thenS14 = new FuzzyRuleConsequent();
thenS14->addOutput(S);

FuzzyRule *fuzzyRule79 = new FuzzyRule(79, ift_And_sk_And_p15, thenS14);
fuzzy->addFuzzyRule(fuzzyRule79);

//80
FuzzyRuleAntecedent *st_And_sk15 = new FuzzyRuleAntecedent();
st_And_sk15->joinWithAND(ST, SK);
FuzzyRuleAntecedent *ifst_And_sk_And_p15 = new FuzzyRuleAntecedent();
ifst_And_sk_And_p15->joinWithAND(st_And_sk15, P);

FuzzyRuleConsequent *thenBK36 = new FuzzyRuleConsequent();
thenBK36->addOutput(BK);

FuzzyRule *fuzzyRule80 = new FuzzyRule(80, ifst_And_sk_And_p15, thenBK36);
fuzzy->addFuzzyRule(fuzzyRule80);

//81
FuzzyRuleAntecedent *sr_And_k16 = new FuzzyRuleAntecedent();
sr_And_k16->joinWithAND(SR, K);
FuzzyRuleAntecedent *ifsr_And_k_And_p16 = new FuzzyRuleAntecedent();
ifsr_And_k_And_p16->joinWithAND(sr_And_k16, P);

FuzzyRuleConsequent *thenBK37 = new FuzzyRuleConsequent();
thenBK37->addOutput(BK);

FuzzyRule *fuzzyRule81 = new FuzzyRule(81, ifsr_And_k_And_p16, thenBK37);
fuzzy->addFuzzyRule(fuzzyRule81);

//82
FuzzyRuleAntecedent *r_And_k16 = new FuzzyRuleAntecedent();
r_And_k16->joinWithAND(R, K);
FuzzyRuleAntecedent *ifr_And_k_And_p16 = new FuzzyRuleAntecedent();
ifr_And_k_And_p16->joinWithAND(r_And_k16, P);

FuzzyRuleConsequent *thenS15 = new FuzzyRuleConsequent();
thenS15->addOutput(S);

FuzzyRule *fuzzyRule82 = new FuzzyRule(82, ifr_And_k_And_p16, thenS15);
fuzzy->addFuzzyRule(fuzzyRule82);

//83
FuzzyRuleAntecedent *b_And_k16 = new FuzzyRuleAntecedent();
b_And_k16->joinWithAND(B, K);
FuzzyRuleAntecedent *ifb_And_k_And_p16 = new FuzzyRuleAntecedent();
ifb_And_k_And_p16->joinWithAND(b_And_k16, P);

FuzzyRuleConsequent *thenA10 = new FuzzyRuleConsequent();
thenA10->addOutput(A);

FuzzyRule *fuzzyRule83 = new FuzzyRule(83, ifb_And_k_And_p16, thenA10);
fuzzy->addFuzzyRule(fuzzyRule83);

//84
FuzzyRuleAntecedent *t_And_k16 = new FuzzyRuleAntecedent();
t_And_k16->joinWithAND(T, K);
FuzzyRuleAntecedent *ift_And_k_And_p16 = new FuzzyRuleAntecedent();
ift_And_k_And_p16->joinWithAND(t_And_k16, P);

FuzzyRuleConsequent *thenS16 = new FuzzyRuleConsequent();
thenS16->addOutput(S);

FuzzyRule *fuzzyRule84 = new FuzzyRule(84, ift_And_k_And_p16, thenS16);
fuzzy->addFuzzyRule(fuzzyRule84);

//85
FuzzyRuleAntecedent *st_And_k16 = new FuzzyRuleAntecedent();
st_And_k16->joinWithAND(ST, K);
FuzzyRuleAntecedent *ifst_And_k_And_p16 = new FuzzyRuleAntecedent();
ifst_And_k_And_p16->joinWithAND(st_And_k16, P);

FuzzyRuleConsequent *thenBK38 = new FuzzyRuleConsequent();
thenBK38->addOutput(BK);

FuzzyRule *fuzzyRule85 = new FuzzyRule(85, ifst_And_k_And_p16, thenBK38);
fuzzy->addFuzzyRule(fuzzyRule85);

//86
FuzzyRuleAntecedent *sr_And_c17 = new FuzzyRuleAntecedent();
sr_And_c17->joinWithAND(SR, C);
FuzzyRuleAntecedent *ifsr_And_c_And_p17 = new FuzzyRuleAntecedent();
ifsr_And_c_And_p17->joinWithAND(sr_And_c17, P);

FuzzyRuleConsequent *thenBK39 = new FuzzyRuleConsequent();
thenBK39->addOutput(BK);

FuzzyRule *fuzzyRule86 = new FuzzyRule(86, ifsr_And_c_And_p17, thenBK39);
fuzzy->addFuzzyRule(fuzzyRule86);

//87
FuzzyRuleAntecedent *r_And_c17 = new FuzzyRuleAntecedent();
r_And_c17->joinWithAND(R, C);
FuzzyRuleAntecedent *ifr_And_c_And_p17 = new FuzzyRuleAntecedent();
ifr_And_c_And_p17->joinWithAND(r_And_c17, P);

FuzzyRuleConsequent *thenS17 = new FuzzyRuleConsequent();
thenS17->addOutput(S);

FuzzyRule *fuzzyRule87= new FuzzyRule(87, ifr_And_c_And_p17, thenS17);
fuzzy->addFuzzyRule(fuzzyRule87);

//88
FuzzyRuleAntecedent *b_And_c17 = new FuzzyRuleAntecedent();
b_And_c17->joinWithAND(B, C);
FuzzyRuleAntecedent *ifb_And_c_And_p17 = new FuzzyRuleAntecedent();
ifb_And_c_And_p17->joinWithAND(b_And_c17, P);

FuzzyRuleConsequent *thenA11 = new FuzzyRuleConsequent();
thenA11->addOutput(A);

FuzzyRule *fuzzyRule88 = new FuzzyRule(88, ifb_And_c_And_p17, thenA11);
fuzzy->addFuzzyRule(fuzzyRule88);

//89
FuzzyRuleAntecedent *t_And_c17 = new FuzzyRuleAntecedent();
t_And_c17->joinWithAND(T, C);
FuzzyRuleAntecedent *ift_And_c_And_p17 = new FuzzyRuleAntecedent();
ift_And_c_And_p17->joinWithAND(t_And_c17, P);

FuzzyRuleConsequent *thenS18 = new FuzzyRuleConsequent();
thenS18->addOutput(S);

FuzzyRule *fuzzyRule89 = new FuzzyRule(89, ift_And_c_And_p17, thenS18);
fuzzy->addFuzzyRule(fuzzyRule89);

//90
FuzzyRuleAntecedent *st_And_c17 = new FuzzyRuleAntecedent();
st_And_c17->joinWithAND(ST, C);
FuzzyRuleAntecedent *ifst_And_c_And_p17 = new FuzzyRuleAntecedent();
ifst_And_c_And_p17->joinWithAND(st_And_c17, P);

FuzzyRuleConsequent *thenBK40 = new FuzzyRuleConsequent();
thenBK40->addOutput(BK);

FuzzyRule *fuzzyRule90 = new FuzzyRule(90, ifst_And_c_And_p17, thenBK40);
fuzzy->addFuzzyRule(fuzzyRule90);

//91
FuzzyRuleAntecedent *sr_And_br18 = new FuzzyRuleAntecedent();
sr_And_br18->joinWithAND(SR, Br);
FuzzyRuleAntecedent *ifsr_And_br_And_p18 = new FuzzyRuleAntecedent();
ifsr_And_br_And_p18->joinWithAND(sr_And_br18, P);

FuzzyRuleConsequent *thenBK41 = new FuzzyRuleConsequent();
thenBK41->addOutput(BK);

FuzzyRule *fuzzyRule91 = new FuzzyRule(91, ifsr_And_br_And_p18, thenBK41);
fuzzy->addFuzzyRule(fuzzyRule91);

//92
FuzzyRuleAntecedent *r_And_br18 = new FuzzyRuleAntecedent();
r_And_br18->joinWithAND(R, Br);
FuzzyRuleAntecedent *ifr_And_br_And_p18 = new FuzzyRuleAntecedent();
ifr_And_br_And_p18->joinWithAND(r_And_br18, P);

FuzzyRuleConsequent *thenBK42 = new FuzzyRuleConsequent();
thenBK42->addOutput(BK);

FuzzyRule *fuzzyRule92 = new FuzzyRule(92, ifr_And_br_And_p18, thenBK42);
fuzzy->addFuzzyRule(fuzzyRule92);

//93
FuzzyRuleAntecedent *b_And_br18 = new FuzzyRuleAntecedent();
b_And_br18->joinWithAND(B, Br);
FuzzyRuleAntecedent *ifb_And_br_And_p18 = new FuzzyRuleAntecedent();
ifb_And_br_And_p18->joinWithAND(b_And_br18, P);

FuzzyRuleConsequent *thenS19 = new FuzzyRuleConsequent();
thenS19->addOutput(S);

FuzzyRule *fuzzyRule93 = new FuzzyRule(93, ifb_And_br_And_p18, thenS19);
fuzzy->addFuzzyRule(fuzzyRule93);

//94
FuzzyRuleAntecedent *t_And_br18 = new FuzzyRuleAntecedent();
t_And_br18->joinWithAND(T, Br);
FuzzyRuleAntecedent *ift_And_br_And_p18 = new FuzzyRuleAntecedent();
ift_And_br_And_p18->joinWithAND(t_And_br18, P);

FuzzyRuleConsequent *thenBK43 = new FuzzyRuleConsequent();
thenBK43->addOutput(BK);

FuzzyRule *fuzzyRule94 = new FuzzyRule(94, ift_And_br_And_p18, thenBK43);
fuzzy->addFuzzyRule(fuzzyRule94);

//95
FuzzyRuleAntecedent *st_And_br18 = new FuzzyRuleAntecedent();
st_And_br18->joinWithAND(ST, Br);
FuzzyRuleAntecedent *ifst_And_br_And_p18 = new FuzzyRuleAntecedent();
ifst_And_br_And_p18->joinWithAND(st_And_br18, P);

FuzzyRuleConsequent *thenBK44 = new FuzzyRuleConsequent();
thenBK44->addOutput(BK);

FuzzyRule *fuzzyRule95 = new FuzzyRule(95, ifst_And_br_And_p18, thenBK44);
fuzzy->addFuzzyRule(fuzzyRule95);

//96
FuzzyRuleAntecedent *sr_And_sbr19 = new FuzzyRuleAntecedent();
sr_And_sbr19->joinWithAND(SR, SBR);
FuzzyRuleAntecedent *ifsr_And_sbr_And_p19 = new FuzzyRuleAntecedent();
ifsr_And_sbr_And_p19->joinWithAND(sr_And_sbr19, P);

FuzzyRuleConsequent *thenSB17 = new FuzzyRuleConsequent();
thenSB17->addOutput(SB);

FuzzyRule *fuzzyRule96 = new FuzzyRule(96, ifsr_And_sbr_And_p19, thenSB17);
fuzzy->addFuzzyRule(fuzzyRule96);

//97
FuzzyRuleAntecedent *r_And_sbr19 = new FuzzyRuleAntecedent();
r_And_sbr19->joinWithAND(R, SBR);
FuzzyRuleAntecedent *ifr_And_sbr_And_p19 = new FuzzyRuleAntecedent();
ifr_And_sbr_And_p19->joinWithAND(r_And_sbr19, P);

FuzzyRuleConsequent *thenBK45 = new FuzzyRuleConsequent();
thenBK45->addOutput(BK);

FuzzyRule *fuzzyRule97 = new FuzzyRule(97, ifr_And_sbr_And_p19, thenBK45);
fuzzy->addFuzzyRule(fuzzyRule97);

//98
FuzzyRuleAntecedent *b_And_sbr19 = new FuzzyRuleAntecedent();
sr_And_sbr19->joinWithAND(B, SBR);
FuzzyRuleAntecedent *ifb_And_sbr_And_p19 = new FuzzyRuleAntecedent();
ifb_And_sbr_And_p19->joinWithAND(b_And_sbr19, P);

FuzzyRuleConsequent *thenBK46 = new FuzzyRuleConsequent();
thenBK46->addOutput(BK);

FuzzyRule *fuzzyRule98 = new FuzzyRule(98, ifb_And_sbr_And_p19, thenBK46);
fuzzy->addFuzzyRule(fuzzyRule98);

//99
FuzzyRuleAntecedent *t_And_sbr19 = new FuzzyRuleAntecedent();
t_And_sbr19->joinWithAND(T, SBR);
FuzzyRuleAntecedent *ift_And_sbr_And_p19 = new FuzzyRuleAntecedent();
ift_And_sbr_And_p19->joinWithAND(t_And_sbr19, P);

FuzzyRuleConsequent *thenBK47 = new FuzzyRuleConsequent();
thenBK47->addOutput(BK);

FuzzyRule *fuzzyRule99 = new FuzzyRule(99, ift_And_sbr_And_p19, thenBK47);
fuzzy->addFuzzyRule(fuzzyRule99);

//100
FuzzyRuleAntecedent *st_And_sbr19 = new FuzzyRuleAntecedent();
st_And_sbr19->joinWithAND(ST, SBR);
FuzzyRuleAntecedent *ifst_And_sbr_And_p19 = new FuzzyRuleAntecedent();
ifst_And_sbr_And_p19->joinWithAND(st_And_sbr19, P);

FuzzyRuleConsequent *thenSB18 = new FuzzyRuleConsequent();
thenSB18->addOutput(SB);

FuzzyRule *fuzzyRule100 = new FuzzyRule(100, ifst_And_sbr_And_p19, thenSB18);
fuzzy->addFuzzyRule(fuzzyRule100);

//101
FuzzyRuleAntecedent *sr_And_sk20 = new FuzzyRuleAntecedent();
sr_And_sk20->joinWithAND(SR, SK);
FuzzyRuleAntecedent *ifsr_And_sk_And_sp20 = new FuzzyRuleAntecedent();
ifsr_And_sk_And_sp20->joinWithAND(sr_And_sk20, SP);

FuzzyRuleConsequent *thenSB19 = new FuzzyRuleConsequent();
thenSB19->addOutput(SB);

FuzzyRule *fuzzyRule101 = new FuzzyRule(101, ifsr_And_sk_And_sp20, thenSB19);
fuzzy->addFuzzyRule(fuzzyRule101);

//102
FuzzyRuleAntecedent *r_And_sk20 = new FuzzyRuleAntecedent();
r_And_sk20->joinWithAND(R, SK);
FuzzyRuleAntecedent *ifr_And_sk_And_sp20 = new FuzzyRuleAntecedent();
ifr_And_sk_And_sp20->joinWithAND(r_And_sk20, SP);

FuzzyRuleConsequent *thenBK48 = new FuzzyRuleConsequent();
thenBK48->addOutput(BK);

FuzzyRule *fuzzyRule102 = new FuzzyRule(102, ifr_And_sk_And_sp20, thenBK48);
fuzzy->addFuzzyRule(fuzzyRule102);

//103
FuzzyRuleAntecedent *b_And_sk20 = new FuzzyRuleAntecedent();
b_And_sk20->joinWithAND(B, SK);
FuzzyRuleAntecedent *ifb_And_sk_And_sp20 = new FuzzyRuleAntecedent();
ifb_And_sk_And_sp20->joinWithAND(b_And_sk20, SP);

FuzzyRuleConsequent *thenBK49 = new FuzzyRuleConsequent();
thenBK49->addOutput(BK);

FuzzyRule *fuzzyRule103 = new FuzzyRule(103, ifb_And_sk_And_sp20, thenBK49);
fuzzy->addFuzzyRule(fuzzyRule102);

//104
FuzzyRuleAntecedent *t_And_sk20 = new FuzzyRuleAntecedent();
sr_And_sk20->joinWithAND(T, SK);
FuzzyRuleAntecedent *ift_And_sk_And_sp20 = new FuzzyRuleAntecedent();
ift_And_sk_And_sp20->joinWithAND(t_And_sk20, SP);

FuzzyRuleConsequent *thenBK50 = new FuzzyRuleConsequent();
thenBK50->addOutput(BK);

FuzzyRule *fuzzyRule104 = new FuzzyRule(104, ift_And_sk_And_sp20, thenBK50);
fuzzy->addFuzzyRule(fuzzyRule104);

//105
FuzzyRuleAntecedent *st_And_sk20 = new FuzzyRuleAntecedent();
st_And_sk20->joinWithAND(ST, SK);
FuzzyRuleAntecedent *ifst_And_sk_And_sp20 = new FuzzyRuleAntecedent();
ifst_And_sk_And_sp20->joinWithAND(st_And_sk20, SP);

FuzzyRuleConsequent *thenSB20 = new FuzzyRuleConsequent();
thenSB20->addOutput(SB);

FuzzyRule *fuzzyRule105 = new FuzzyRule(105, ifst_And_sk_And_sp20, thenSB20);
fuzzy->addFuzzyRule(fuzzyRule105);

//106
FuzzyRuleAntecedent *sr_And_k21 = new FuzzyRuleAntecedent();
sr_And_k21->joinWithAND(SR, K);
FuzzyRuleAntecedent *ifsr_And_k_And_sp21 = new FuzzyRuleAntecedent();
ifsr_And_k_And_sp21->joinWithAND(sr_And_k21, SP);

FuzzyRuleConsequent *thenSB21 = new FuzzyRuleConsequent();
thenSB21->addOutput(SB);

FuzzyRule *fuzzyRule106 = new FuzzyRule(106, ifsr_And_k_And_sp21, thenSB21);
fuzzy->addFuzzyRule(fuzzyRule106);

//107
FuzzyRuleAntecedent *r_And_k21 = new FuzzyRuleAntecedent();
r_And_k21->joinWithAND(R, K);
FuzzyRuleAntecedent *ifr_And_k_And_sp21 = new FuzzyRuleAntecedent();
ifr_And_k_And_sp21->joinWithAND(r_And_k21, SP);

FuzzyRuleConsequent *thenBK51 = new FuzzyRuleConsequent();
thenBK51->addOutput(BK);

FuzzyRule *fuzzyRule107 = new FuzzyRule(107, ifr_And_k_And_sp21, thenBK51);
fuzzy->addFuzzyRule(fuzzyRule107);

//108
FuzzyRuleAntecedent *b_And_k21 = new FuzzyRuleAntecedent();
b_And_k21->joinWithAND(B, K);
FuzzyRuleAntecedent *ifb_And_k_And_sp21 = new FuzzyRuleAntecedent();
ifb_And_k_And_sp21->joinWithAND(b_And_k21, SP);

FuzzyRuleConsequent *thenBK52 = new FuzzyRuleConsequent();
thenBK52->addOutput(BK);

FuzzyRule *fuzzyRule108 = new FuzzyRule(108, ifb_And_k_And_sp21, thenBK52);
fuzzy->addFuzzyRule(fuzzyRule108);

//109
FuzzyRuleAntecedent *t_And_k21 = new FuzzyRuleAntecedent();
t_And_k21->joinWithAND(T, K);
FuzzyRuleAntecedent *ift_And_k_And_sp21 = new FuzzyRuleAntecedent();
ift_And_k_And_sp21->joinWithAND(t_And_k21, SP);

FuzzyRuleConsequent *thenBK53 = new FuzzyRuleConsequent();
thenBK53->addOutput(BK);

FuzzyRule *fuzzyRule109 = new FuzzyRule(109, ift_And_k_And_sp21, thenBK53);
fuzzy->addFuzzyRule(fuzzyRule109);

//110
FuzzyRuleAntecedent *st_And_k21 = new FuzzyRuleAntecedent();
st_And_k21->joinWithAND(ST, K);
FuzzyRuleAntecedent *ifst_And_k_And_sp21 = new FuzzyRuleAntecedent();
ifst_And_k_And_sp21->joinWithAND(st_And_k21, SP);

FuzzyRuleConsequent *thenSB22 = new FuzzyRuleConsequent();
thenSB22->addOutput(SB);

FuzzyRule *fuzzyRule110 = new FuzzyRule(110, ifst_And_k_And_sp21, thenSB22);
fuzzy->addFuzzyRule(fuzzyRule110);

//111
FuzzyRuleAntecedent *sr_And_c22 = new FuzzyRuleAntecedent();
sr_And_c22->joinWithAND(SR, C);
FuzzyRuleAntecedent *ifsr_And_c_And_sp22 = new FuzzyRuleAntecedent();
ifsr_And_c_And_sp22->joinWithAND(sr_And_c22, SP);

FuzzyRuleConsequent *thenSB23 = new FuzzyRuleConsequent();
thenSB23->addOutput(SB);

FuzzyRule *fuzzyRule111 = new FuzzyRule(111, ifsr_And_c_And_sp22, thenSB23);
fuzzy->addFuzzyRule(fuzzyRule111);

//112
FuzzyRuleAntecedent *r_And_c22 = new FuzzyRuleAntecedent();
r_And_c22->joinWithAND(R, C);
FuzzyRuleAntecedent *ifr_And_c_And_sp22 = new FuzzyRuleAntecedent();
ifr_And_c_And_sp22->joinWithAND(r_And_c22, SP);

FuzzyRuleConsequent *thenBK54 = new FuzzyRuleConsequent();
thenBK54->addOutput(BK);

FuzzyRule *fuzzyRule112 = new FuzzyRule(112, ifr_And_c_And_sp22, thenBK54);
fuzzy->addFuzzyRule(fuzzyRule112);

//113
FuzzyRuleAntecedent *b_And_c22 = new FuzzyRuleAntecedent();
b_And_c22->joinWithAND(B, C);
FuzzyRuleAntecedent *ifb_And_c_And_sp22 = new FuzzyRuleAntecedent();
ifb_And_c_And_sp22->joinWithAND(b_And_c22, SP);

FuzzyRuleConsequent *thenBK55 = new FuzzyRuleConsequent();
thenBK55->addOutput(BK);

FuzzyRule *fuzzyRule113 = new FuzzyRule(113, ifb_And_c_And_sp22, thenBK55);
fuzzy->addFuzzyRule(fuzzyRule113);

//114
FuzzyRuleAntecedent *t_And_c22 = new FuzzyRuleAntecedent();
t_And_c22->joinWithAND(T, C);
FuzzyRuleAntecedent *ift_And_c_And_sp22 = new FuzzyRuleAntecedent();
ift_And_c_And_sp22->joinWithAND(t_And_c22, SP);

FuzzyRuleConsequent *thenBK56 = new FuzzyRuleConsequent();
thenBK56->addOutput(BK);

FuzzyRule *fuzzyRule114 = new FuzzyRule(114, ift_And_c_And_sp22, thenBK56);
fuzzy->addFuzzyRule(fuzzyRule114);

//115
FuzzyRuleAntecedent *st_And_c22 = new FuzzyRuleAntecedent();
st_And_c22->joinWithAND(ST, C);
FuzzyRuleAntecedent *ifst_And_c_And_sp22 = new FuzzyRuleAntecedent();
ifst_And_c_And_sp22->joinWithAND(st_And_c22, SP);

FuzzyRuleConsequent *thenSB24 = new FuzzyRuleConsequent();
thenSB24->addOutput(SB);

FuzzyRule *fuzzyRule115 = new FuzzyRule(115, ifst_And_c_And_sp22, thenSB24);
fuzzy->addFuzzyRule(fuzzyRule115);

//116
FuzzyRuleAntecedent *sr_And_br23 = new FuzzyRuleAntecedent();
sr_And_br23->joinWithAND(SR, Br);
FuzzyRuleAntecedent *ifsr_And_br_And_sp23 = new FuzzyRuleAntecedent();
ifsr_And_br_And_sp23->joinWithAND(sr_And_br23, SP);

FuzzyRuleConsequent *thenSB25 = new FuzzyRuleConsequent();
thenSB25->addOutput(SB);

FuzzyRule *fuzzyRule116 = new FuzzyRule(116, ifsr_And_br_And_sp23, thenSB25);
fuzzy->addFuzzyRule(fuzzyRule116);

//117
FuzzyRuleAntecedent *r_And_br23 = new FuzzyRuleAntecedent();
r_And_br23->joinWithAND(R, Br);
FuzzyRuleAntecedent *ifr_And_br_And_sp23 = new FuzzyRuleAntecedent();
ifr_And_br_And_sp23->joinWithAND(r_And_br23, SP);

FuzzyRuleConsequent *thenBK57 = new FuzzyRuleConsequent();
thenBK57->addOutput(BK);

FuzzyRule *fuzzyRule117 = new FuzzyRule(117, ifr_And_br_And_sp23, thenBK57);
fuzzy->addFuzzyRule(fuzzyRule117);

//118
FuzzyRuleAntecedent *b_And_br23 = new FuzzyRuleAntecedent();
b_And_br23->joinWithAND(B, Br);
FuzzyRuleAntecedent *ifb_And_br_And_sp23 = new FuzzyRuleAntecedent();
ifb_And_br_And_sp23->joinWithAND(b_And_br23, SP);

FuzzyRuleConsequent *thenBK58 = new FuzzyRuleConsequent();
thenBK58->addOutput(BK);

FuzzyRule *fuzzyRule118 = new FuzzyRule(118, ifb_And_br_And_sp23, thenBK58);
fuzzy->addFuzzyRule(fuzzyRule118);

//119
FuzzyRuleAntecedent *t_And_br23 = new FuzzyRuleAntecedent();
t_And_br23->joinWithAND(T, Br);
FuzzyRuleAntecedent *ift_And_br_And_sp23 = new FuzzyRuleAntecedent();
ift_And_br_And_sp23->joinWithAND(t_And_br23, SP);

FuzzyRuleConsequent *thenBK59 = new FuzzyRuleConsequent();
thenBK59->addOutput(BK);

FuzzyRule *fuzzyRule119 = new FuzzyRule(119, ift_And_br_And_sp23, thenBK59);
fuzzy->addFuzzyRule(fuzzyRule119);

//120
FuzzyRuleAntecedent *st_And_br23 = new FuzzyRuleAntecedent();
st_And_br23->joinWithAND(ST, Br);
FuzzyRuleAntecedent *ifst_And_br_And_sp23 = new FuzzyRuleAntecedent();
ifst_And_br_And_sp23->joinWithAND(st_And_br23, SP);

FuzzyRuleConsequent *thenSB26 = new FuzzyRuleConsequent();
thenSB26->addOutput(SB);

FuzzyRule *fuzzyRule120 = new FuzzyRule(120, ifst_And_br_And_sp23, thenSB26);
fuzzy->addFuzzyRule(fuzzyRule120);

//121
FuzzyRuleAntecedent *sr_And_sbr24 = new FuzzyRuleAntecedent();
sr_And_sbr24->joinWithAND(SR, SBR);
FuzzyRuleAntecedent *ifsr_And_sbr_And_sp24 = new FuzzyRuleAntecedent();
ifsr_And_sbr_And_sp24->joinWithAND(sr_And_sbr24, SP);

FuzzyRuleConsequent *thenSB27 = new FuzzyRuleConsequent();
thenSB27->addOutput(SB);

FuzzyRule *fuzzyRule121 = new FuzzyRule(121, ifsr_And_sbr_And_sp24, thenSB27);
fuzzy->addFuzzyRule(fuzzyRule121);

//122
FuzzyRuleAntecedent *r_And_sbr24 = new FuzzyRuleAntecedent();
r_And_sbr24->joinWithAND(R, SBR);
FuzzyRuleAntecedent *ifr_And_sbr_And_sp24 = new FuzzyRuleAntecedent();
ifr_And_sbr_And_sp24->joinWithAND(r_And_sbr24, SP);

FuzzyRuleConsequent *thenSB28 = new FuzzyRuleConsequent();
thenSB28->addOutput(SB);

FuzzyRule *fuzzyRule122 = new FuzzyRule(122, ifr_And_sbr_And_sp24, thenSB28);
fuzzy->addFuzzyRule(fuzzyRule122);

//123
FuzzyRuleAntecedent *b_And_sbr24 = new FuzzyRuleAntecedent();
b_And_sbr24->joinWithAND(B, SBR);
FuzzyRuleAntecedent *ifb_And_sbr_And_sp24 = new FuzzyRuleAntecedent();
ifb_And_sbr_And_sp24->joinWithAND(b_And_sbr24, SP);

FuzzyRuleConsequent *thenSB29 = new FuzzyRuleConsequent();
thenSB29->addOutput(SB);

FuzzyRule *fuzzyRule123 = new FuzzyRule(123, ifb_And_sbr_And_sp24, thenSB29);
fuzzy->addFuzzyRule(fuzzyRule123);

//124
FuzzyRuleAntecedent *t_And_sbr24 = new FuzzyRuleAntecedent();
t_And_sbr24->joinWithAND(T, SBR);
FuzzyRuleAntecedent *ift_And_sbr_And_sp24 = new FuzzyRuleAntecedent();
ift_And_sbr_And_sp24->joinWithAND(t_And_sbr24, SP);

FuzzyRuleConsequent *thenSB30 = new FuzzyRuleConsequent();
thenSB30->addOutput(SB);

FuzzyRule *fuzzyRule124 = new FuzzyRule(124, ift_And_sbr_And_sp24, thenSB30);
fuzzy->addFuzzyRule(fuzzyRule124);

//125
FuzzyRuleAntecedent *st_And_sbr24 = new FuzzyRuleAntecedent();
t_And_sbr24->joinWithAND(ST, SBR);
FuzzyRuleAntecedent *ifst_And_sbr_And_sp24 = new FuzzyRuleAntecedent();
ifst_And_sbr_And_sp24->joinWithAND(st_And_sbr24, SP);

FuzzyRuleConsequent *thenSB31 = new FuzzyRuleConsequent();
thenSB31->addOutput(SB);

FuzzyRule *fuzzyRule125 = new FuzzyRule(125, ifst_And_sbr_And_sp24, thenSB31);
fuzzy->addFuzzyRule(fuzzyRule125);
//----------------------------------------------------------------------------------------------------------------
}

//=================================================
String getMonthString(struct tm t) {
  char buf[10];
  sprintf(buf, "%04d-%02d", t.tm_year + 1900, t.tm_mon + 1);
  return String(buf);
}

String getDayString(struct tm t) {
  char buf[3];
  sprintf(buf, "%02d", t.tm_mday);
  return String(buf);
}

// cleanupOldMonths: hapus bulan tertua jika jumlah bulan > 3
void cleanupOldMonths() {
  FirebaseJson json;
  if (!Firebase.RTDB.getJSON(&fbdo, "/chart_bulanan", &json)) {
    Serial.print("cleanupOldMonths: gagal getJSON -> ");
    Serial.println(fbdo.errorReason());
    return;
  }

  size_t count = json.iteratorBegin();
  if (count > 3) {
    int type;
    String key, value;
    // ambil key pertama (paling lama)
    json.iteratorGet(0, type, key, value);
    String deletePath = "/chart_bulanan/" + key;
    if (Firebase.RTDB.deleteNode(&fbdo, deletePath)) {
      Serial.print("cleanupOldMonths: deleted ");
      Serial.println(deletePath);
    } else {
      Serial.print("cleanupOldMonths: gagal delete -> ");
      Serial.println(fbdo.errorReason());
    }
  }
  json.iteratorEnd();
}

//=================================================

void loop() {
  // Ambil data suhu dari sensor
  sensors.requestTemperatures(); 
  float temperatureC = sensors.getTempCByIndex(0);

  // Tampilkan suhu di LCD
  //lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tmp:");
  lcd.print(temperatureC);
  lcd.print((char)223);
  lcd.print("C");
  //Serial.println(temperatureC);

  // Kirim nilai suhu ke Firebase
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    // Tulis data
   if (Firebase.RTDB.setFloat(&fbdo, "/sensor/suhu", temperatureC)) {
      //Serial.print("Nilai suhu terkirim: ");
      //Serial.println(temperatureC);
    } else {
      Serial.println("Gagal mengirim nilai suhu ke Firebase.");
      Serial.println("Alasan: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Firebase not ready or WiFi disconnected");
  }
    

  //delay(1000); // Delay untuk menghindari pembacaan berulang yang terlalu cepat

  // SENSOR AMONIAK
  float sensorVoltage = analogRead(mq135Pin) * (3.3 / 4095.0);
  float RS = ((3.3 / sensorVoltage) - 1) * R0; // Hitung resistansi sensor
  //float R0 = RS / 3.6; //kalibrasi R0

  // Kurva karakteristik dari datasheet MQ-135 untuk gas NH3
  float ratio = RS / R0;
  float ppm = 116.6020682 * pow(ratio, -2.769034857); // Rumus untuk menghitung PPM

  //lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("NH3:");
  lcd.print(ppm);
  lcd.print("ppm");
  //Serial.println(ppm);
  //delay(1000); // Delay untuk menghindari pembacaan berulang yang terlalu cepat
  // Kirim nilai suhu ke Firebase
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    // Tulis data
   if (Firebase.RTDB.setFloat(&fbdo, "/sensor/amoniak", ppm)) {
      //Serial.print("Nilai amoniak terkirim: ");
      //Serial.println(ppm);
    } else {
      Serial.println("Gagal mengirim nilai amoniak ke Firebase.");
      Serial.println("Alasan: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Firebase not ready or WiFi disconnected");
  }

  //SENSOR PH
  nilai_analog_PH = analogRead(ph_Pin);
  //voltage = nilai_analog_PH * (3.3 / 1023.0);
  //Serial.println(voltage);
  //Serial.print("Nilai ADC PH : ");
  //Serial.println(nilai_analog_PH);
  TeganganPh = 3.3 / 1023.0 * nilai_analog_PH;
  //Serial.print("TeganganPH : ");
  //Serial.println(TeganganPh, 3);
  

  PH_step = (PH4 - PH7) / 3;
  Po = 7.00 + ((PH7 - TeganganPh) / PH_step);
  //Serial.print("Nilai PH : ");
  //Serial.println(Po, 2);
  lcd.setCursor(12, 0);
  lcd.print("PH");
  lcd.setCursor(12, 1);
  lcd.print(Po);

  // Kirim nilai suhu ke Firebase
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    // Tulis data
   if (Firebase.RTDB.setFloat(&fbdo, "/sensor/ph", Po)) {
      //Serial.print("Nilai ph terkirim: ");
      //Serial.println(Po);
    } else {
      Serial.println("Gagal mengirim nilai ph ke Firebase.");
      Serial.println("Alasan: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Firebase not ready or WiFi disconnected");
  }
   

//=====LOOP FUZZY=====
float inputPh = Po;
float inputAmonia = ppm;
float inputSuhu = temperatureC;

  Serial.print("\t\t\tPH: ");
  Serial.print(inputPh);
  Serial.print(", AMONIA: ");
  Serial.print(inputAmonia);
  Serial.print(", and SUHU: ");
  Serial.println(inputSuhu);

  fuzzy->setInput(1, inputPh);
  fuzzy->setInput(2, inputAmonia);
  fuzzy->setInput(3, inputSuhu);

  fuzzy->fuzzify();

  Serial.println("Input PH: ");
  Serial.print("\tPH: pHsangat_rendah-> ");
  Serial.print(SR->getPertinence());
  Serial.print(", PHRendah-> ");
  Serial.print(R->getPertinence());
  Serial.print(", PHBaik-> ");
  Serial.println(B->getPertinence());
  Serial.print(", PHTinggi-> ");
  Serial.println(T->getPertinence());
  Serial.print(", PHsangat_tinggi-> ");
  Serial.println(ST->getPertinence());

  Serial.println("Input AMONIA: ");
  Serial.print("\tAMONIA: amonSangat_Kecil-> ");
  Serial.print(SK->getPertinence());
  Serial.print(", AMONIAkecil-> ");
  Serial.print(K->getPertinence());
  Serial.print(", AMONIAcukup-> ");
  Serial.println(C->getPertinence());
  Serial.print(", AMONIAbesar-> ");
  Serial.println(Br->getPertinence());
  Serial.print(", AMONIAsangat_besar-> ");
  Serial.println(SBR->getPertinence());

  Serial.println("Input SUHU: ");
  Serial.print("\tSUHU: sangat_dingin-> ");
  Serial.print(Sd->getPertinence());
  Serial.print(", suhuDingin-> ");
  Serial.print(D->getPertinence());
  Serial.print(", suhuNormal-> ");
  Serial.println(N->getPertinence());
  Serial.print(", suhuPanas-> ");
  Serial.println(P->getPertinence());
  Serial.print(", suhuSangat_panas-> ");
  Serial.println(SP->getPertinence());

  float output1 = fuzzy->defuzzify(1);

  Serial.println("Output: ");
  Serial.print("\tRisk: Sangat_Aman-> ");
  Serial.print(SA->getPertinence());
  Serial.print(", Aman-> ");
  Serial.print(A->getPertinence());
  Serial.print(", Sedang-> ");
  Serial.println(S->getPertinence());
  Serial.print(", Buruk-> ");
  Serial.println(BK->getPertinence());
  Serial.print(", Sangat_Buruk-> ");
  Serial.println(SB->getPertinence());

  Serial.println("Result: ");
  Serial.print("\t\t\tRisk: ");
  Serial.print(output1);

  
  Serial.print("\t\t\tRAW: ");
  Serial.println(sensorVoltage); // Mencetak pembacaan mentah ADC
 // Serial.print("\t\t\tR0 : ");
 // Serial.println(R0);

  // Kirim nilai out ke Firebase
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    // Tulis data
   if (Firebase.RTDB.setFloat(&fbdo, "/sensor/risk", output1)) {
      //Serial.print("Nilai risk terkirim: ");
      //Serial.println(output1);
    } else {
      Serial.println("Gagal mengirim nilai ph ke Firebase.");
      Serial.println("Alasan: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Firebase not ready or WiFi disconnected");
  }
    
  // Penilaian air
  float air = 100-output1;
  penilaianBulanan = air;
  Serial.print("Penilaian : ");
  Serial.print(air);
  Firebase.RTDB.setFloat(&fbdo, "/sensor/penilaian", air);

  //kondisi
  const char *k_1 = "SANGAT AMAN";
  const char *k_2 = "AMAN";
  const char *k_3 = "SEDANG";
  const char *k_4 = "BURUK";
  const char *k_5 = "SANGAT BURUK";

  //prediksi
  const char *sangatAman = "0";
  const char *aman = "15";
  const char *sedang = "30";
  const char *buruk = "50";
  const char *sangatBuruk = "70";
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    // Tulis data
   if (output1 <= 15){
    Serial.print(sangatAman);
    Firebase.RTDB.setString(&fbdo, "/sensor/persen", sangatAman);
    Serial.print(k_1);
    Firebase.RTDB.setString(&fbdo, "/sensor/kondisi", k_1);
  } else if (output1 <= 30){
      Serial.print(aman);
      Firebase.RTDB.setString(&fbdo, "/sensor/persen", aman);
      Serial.print(k_2);
      Firebase.RTDB.setString(&fbdo, "/sensor/kondisi", k_2);
  } else if (output1 <= 50){
      Serial.print(sedang);
      Firebase.RTDB.setString(&fbdo, "/sensor/persen", sedang);
      Serial.print(k_3);
      Firebase.RTDB.setString(&fbdo, "/sensor/kondisi", k_3);
  } else if (output1 <= 70){
      Serial.print(buruk);
      Firebase.RTDB.setString(&fbdo, "/sensor/persen", buruk);
      Serial.print(k_4);
      Firebase.RTDB.setString(&fbdo, "/sensor/kondisi", k_4);
  } else if (output1 <= 100){
      Serial.print(sangatBuruk);
      Firebase.RTDB.setString(&fbdo, "/sensor/persen", sangatBuruk);
      Serial.print(k_5);
      Firebase.RTDB.setString(&fbdo, "/sensor/kondisi", k_5);
  }
  } else {
    Serial.println("Firebase not ready or WiFi disconnected");
  }
  
// PENCATATAN HARIAN
  float penilaian = air;  

  String today = getTodayDate();

  if (isNineAM() && lastUpload != today) {

    String base = "/chart/harian/" + today;

    Firebase.RTDB.setFloat(&fbdo, base + "/penilaian", penilaian);
    Firebase.RTDB.setString(&fbdo, base + "/tanggal", today);

    Serial.println("Upload harian berhasil: " + today);

    lastUpload = today;

    delay(30000); // jaga agar tidak upload 2x
  }

  cleanupOldData();
// pencatatan jam 9 harian ke bulanan
struct tm timeinfo;
  if (!getTime(timeinfo)) return;

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;

  String dayKey = getDayString(timeinfo);
  String monthKey = getMonthString(timeinfo);
  String fullDateKey = monthKey + "-" + dayKey;

  Serial.printf("Jam sekarang: %02d:%02d (%s)\n", hour, minute, fullDateKey.c_str());

  // ============ UPLOAD HARIAN ==============
  if (hour == 22 && minute == 55 && lastDailyUpload != fullDateKey) {

      String path = "/chart_bulanan/" + monthKey + "/" + dayKey;

      bool ok = Firebase.RTDB.setFloat(&fbdo, path + "/penilaian", penilaianBulanan);

      if (ok) {
        Serial.println("UPLOAD BULANAN BERHASIL: " + path);
      } else {
        Serial.println("GAGAL UPLOAD: " + fbdo.errorReason());
      }

      lastDailyUpload = fullDateKey;  // FIX
  }

  // ====== CLEANUP BULANAN (jalankan 1x per hari) ======
  if (hour == 22 && minute == 53 && lastCleanup != fullDateKey) {
      cleanupOldMonths();
      lastCleanup = fullDateKey;
}


  delay(5000);
}
