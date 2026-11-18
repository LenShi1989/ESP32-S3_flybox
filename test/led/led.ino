#define LED_PIN 39

void setup()
{
  Serial.begin(115200);
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_PIN, OUTPUT);
}

// the loop function runs over and over again forever
void loop()
{
  Serial.println("Turning LED on");
  digitalWrite(LED_PIN, HIGH); // turn the LED on (HIGH is the voltage level)
  delay(1000);                 // wait for a second
  Serial.println("Turning LED off");
  digitalWrite(LED_PIN, LOW); // turn the LED off by making the voltage LOW
  delay(1000);                // wait for a second
}