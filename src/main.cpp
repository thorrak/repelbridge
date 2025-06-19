// #include <Arduino.h>
// #include <SoftwareSerial.h>

// // Define the TX pin for SoftwareSerial (use any suitable GPIO except 1/3/16/17)
// const uint8_t txPin = 4;         // GPIO4 = D2 on NodeMCU
// const uint8_t mirroredPin = 5;   // Optional mirror pin (e.g., GPIO5 = D1)

// SoftwareSerial softSerial(12, txPin);  // RX not used


// // Example data captured from your log
// // const uint8_t serialData[] = {
// //   0xFF, 0xFF, 0xFF, 0xDE, 0xFF, 0xFF, 0xAD, 0xFF, 0xFF, 0xDE, 0xFF
// // };

// // const size_t dataLength = sizeof(serialData);


// const uint8_t running_heartbeat[] = {
//   0xAA, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
// };

// const uint8_t shutdown[] = {
//   0xAA, 0x8E, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
// };

// const uint8_t startup_heartbeat[] = {
//   0xAA, 0x82, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
// };



// void setup() {
//   Serial.begin(115200);          // Debug serial
//   softSerial.begin(19200);       // Emulated serial line

//   pinMode(mirroredPin, OUTPUT);
//   digitalWrite(mirroredPin, LOW);
// }


// void loop() {
//   for (size_t i = 0; i < sizeof(startup_heartbeat); ++i) {
//     uint8_t byteToSend = startup_heartbeat[i];
//     softSerial.write(byteToSend);

//     // Optional: mirror activity (HIGH for "active", LOW for "idle")
//     digitalWrite(mirroredPin, byteToSend > 0x80 ? HIGH : LOW);

//     delayMicroseconds(500);  // Approximate inter-byte delay
//   }

//   delay(15000);  // Delay before repeating
// }



#include <Arduino.h>

// Serial1 TX is fixed on GPIO2 (D4 on NodeMCU)
// TX pin for Serial1 (GPIO2 = D4)
const uint8_t rs485_tx_pin = 2;

// DE and RE (tied together) control pin
const uint8_t rs485_direction_pin = 4;  // GPIO4 = D2



// Your message arrays
const uint8_t running_heartbeat[] = {
  0xAA, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t shutdown[] = {
  0xAA, 0x8E, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t startup_heartbeat[] = {
  0xAA, 0x82, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t post_startup_heartbeat[] = {
    0xAA, 0x82, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void write_and_release(const uint8_t* msg, size_t length) {
  // Set RS-485 transceiver to transmit mode
  digitalWrite(rs485_direction_pin, HIGH);
  // delayMicroseconds(10);  // Give DE time to enable
  delay(99);

  // Send the message
  Serial1.write(msg, length);
  Serial1.flush();  // Wait until transmission complete

  // Back to receive mode
  delayMicroseconds(10);  // Give DE time to enable
  digitalWrite(rs485_direction_pin, LOW);
}



void write_startup() {


  write_and_release(startup_heartbeat, sizeof(startup_heartbeat));
  delay(1000);
  
  for(uint8_t i=0x01; i<=0x21; i++) {
    uint8_t msg[] = {0xAA, i, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    write_and_release(msg, sizeof(msg));
  }

  for(uint8_t i=0; i<3; i++) {
    write_and_release(post_startup_heartbeat, sizeof(post_startup_heartbeat));
    delay(3200);
  }

  uint8_t msg[] = {0xAA, 0xFA, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  write_and_release(msg, sizeof(msg));

}


void setup() {
  Serial.begin(115200);
  Serial.println("RS-485 Test Starting...");

  // Set DE/RE control pin as output
  pinMode(rs485_direction_pin, OUTPUT);
  digitalWrite(rs485_direction_pin, LOW);  // Start in receive mode

  // Start Serial1 (TX on GPIO2)
  Serial1.begin(19200);
  delay(100);

  delay(3000);
  write_startup();
  delay(20000);

}

void loop() {
  Serial.println("Sending startup_heartbeat");

  // Send the message
  write_and_release(startup_heartbeat, sizeof(startup_heartbeat));
  Serial1.flush();  // Wait until transmission complete

  Serial.println("Message sent");
  delay(15000);
}