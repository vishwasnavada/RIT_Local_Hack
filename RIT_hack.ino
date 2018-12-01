#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#define  MQ_PIN  (A0)

const int trigPin = 2;  //D4
const int echoPin = 0;  //D3
#define         RL_VALUE                     (5)
// defines variables
#define         CALIBARAION_SAMPLE_TIMES     (50)    //define how many samples you are going to take in the calibration phase
#define         CALIBRATION_SAMPLE_INTERVAL  (500)   //define the time interal(in milisecond) between each samples in the
//cablibration phase
#define         READ_SAMPLE_INTERVAL         (50)    //define how many samples you are going to take in normal operation
#define         READ_SAMPLE_TIMES            (30)     //define the time interal(in milisecond) between each samples in 
#define         RO_CLEAN_AIR_FACTOR          (9.83)
#define         GAS_LPG                      (0)
#define         GAS_CO                       (1)
#define     GAS_SMOKE          (2)
#define         GAS_CARBON_MONOXIDE          (3)
#define         GAS_METHANE                  (4)
long duration;
int distance;

int upload_count = 0;
float           LPGCurve[3]  =  {2.3, 0.21, -0.47}; //two points are taken from the curve.
//with these two points, a line is formed which is "approximately equivalent"
//to the original curve.
//data format:{ x, y, slope}; point1: (lg200, 0.21), point2: (lg10000, -0.59)
float           COCurve[3]  =  {2.3, 0.72, -0.34};  //two points are taken from the curve.
//with these two points, a line is formed which is "approximately equivalent"
//to the original curve.
//data format:{ x, y, slope}; point1: (lg200, 0.72), point2: (lg10000,  0.15)
float           SmokeCurve[3] = {2.3, 0.53, -0.44}; //two points are taken from the curve.
//with these two points, a line is formed which is "approximately equivalent"
//to the original curve.
//data format:{ x, y, slope}; point1: (lg200, 0.53), point2: (lg10000,  -0.22)
float           Ro           =  10;                 //Ro is initialized to 10 kilo ohms
float           Ro_9         =  20;

float GAS_S = 0;      float CO = 0;
int count = 0;
int sent = 0;


const char* MY_SSID = "Ironman";
const char* MY_PWD = "@Roomno52";
const char* host = "192.168.43.6";

int  MQGetPercentage(float rs_ro_ratio, float *pcurve)
{
  return (pow(10, ( ((log(rs_ro_ratio) - pcurve[1]) / pcurve[2]) + pcurve[0])));
}

float MQ2ResistanceCalculation(int raw_adc)
{
  return ( ((float)RL_VALUE * (1023 - raw_adc) / raw_adc));
}

float MQ2Calibration(int mq_pin)
{
  int i;
  float val = 0;

  for (i = 0; i < CALIBARAION_SAMPLE_TIMES; i++) {      //take multiple samples
    val += MQ2ResistanceCalculation(analogRead(mq_pin));
    delay(CALIBRATION_SAMPLE_INTERVAL);
  }
  val = val / CALIBARAION_SAMPLE_TIMES;                 //calculate the average value

  val = val / RO_CLEAN_AIR_FACTOR;                      //divided by RO_CLEAN_AIR_FACTOR yields the Ro
  //according to the chart in the datasheet
  Serial.print("MQ2 done.\n");
  return val;
}
void connectWifi()
{
  Serial.print("Connecting to " + *MY_SSID);
  WiFi.begin(MY_SSID, MY_PWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Connected");
  Serial.println("");
}
int MQGetGasPercentage(float rs_ro_ratio, int gas_id)
{
  if ( gas_id == GAS_LPG ) {
    return MQGetPercentage(rs_ro_ratio, LPGCurve);
  } else if ( gas_id == GAS_CO ) {
    return MQGetPercentage(rs_ro_ratio, COCurve);
  } else if ( gas_id == GAS_SMOKE ) {
    return MQGetPercentage(rs_ro_ratio, SmokeCurve);
  }
  return 0;
}

void setup()
{

  Serial.begin(9600);
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input
}
void Calibrating() {

  Serial.print("Calibrating...\n");
  Ro = MQ2Calibration(MQ_PIN);
  Serial.print("Ro=");
  Serial.print(Ro);
  Serial.print("kohm\t");
  Serial.print("Ro_9=");
  Serial.print(Ro_9);
  Serial.print("kohm");
  Serial.print("\n");

}

void SentOnCloud( String S, String CO, int distance)
{
  Serial.print("connecting to ");
  Serial.println(host);

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return;
  }

  // We now create a URI for the request
  String url = "/apartment/apt1.php?";
  url += "s1=";
  url += GAS_S;
  url += "&s2=";
  url += CO;
  url += "&s3=";
  url += distance;


  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("closing connection");
}


float MQ2Read(int mq_pin)
{
  int i;
  float rs = 0;
  float j;
  for (i = 0; i < READ_SAMPLE_TIMES; i++) {
    rs += MQ2ResistanceCalculation(analogRead(mq_pin));
    j = analogRead(mq_pin);
    delay(READ_SAMPLE_INTERVAL);
  }
  rs = rs / READ_SAMPLE_TIMES;

  return rs;
}

void loop()
{
  int index = 0;
  char value;
  char previousValue;
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);

  // Calculating the distance
  distance = duration * 0.034 / 2;
  // Prints the distance on the Serial Monitor
  Serial.print("Distance: ");
  Serial.println(distance);
  delay(2000);
  GAS_S = MQGetGasPercentage(MQ2Read(MQ_PIN) / Ro, GAS_SMOKE);
  Serial.println("Gas sensor value:");
  Serial.print("CH4 level ");  Serial.print(GAS_S);   Serial.println( "ppm" );
  upload_count++;
  if (upload_count == 10) {
    connectWifi();
    SentOnCloud(String(GAS_S), String(CO), distance);
    WiFi.disconnect();
    upload_count = 0;
  }
}
