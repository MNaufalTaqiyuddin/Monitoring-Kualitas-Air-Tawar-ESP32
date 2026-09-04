# Monitoring-Kualitas-Air-Tawar-ESP32
# Sistem Monitoring Kualitas Air Tawar (ESP32 & Fuzzy Logic Mamdani)

Proyek ini merupakan sistem monitoring kualitas air tawar berbasis Embedded IoT yang mengukur parameter fisik air secara real-time dan mengklasifikasikan tingkat kelayakan air, memberi saran persentase air yang harus dikuras menggunakan algoritma **Fuzzy Logic Mamdani**.

## Tech Stack & Hardware
- **Microcontroller:** ESP32 DevkitV1
- **Sensors:** DS18B20 (Suhu), MQ-135(Amoniak), pH Sensor + Modul pH-4502C (pH)
- **Monitor** : LCD 16 x 2 + I2C
- **Cloud Database:** Firebase Realtime Database
- **Programming Language:** C++ (Embedded), HTML/JS (Dashboard Web)
- **Algorithm:** Fuzzy Logic Mamdani

## Fitur Utama
1. **Multi-Sensor Data Acquisition:** Membaca parameter air secara konstan.
2. **Fuzzy Logic Inference:** Mengolah multi-parameter untuk menentukan status kelayakan air.
3. **Real-Time Cloud Sync:** Mengirim data sensor secara instan ke Firebase untuk ditampilkan di dashboard web.

## Dokumentasi
<img width="814" height="556" alt="image" src="https://github.com/user-attachments/assets/59c4866e-07f1-4096-a2e5-ce108840f44a" />
<img width="827" height="581" alt="image (1)" src="https://github.com/user-attachments/assets/5fb804da-0637-4301-8e27-9f74363c19f9" />
<img width="814" height="404" alt="image (2)" src="https://github.com/user-attachments/assets/07a1b56e-eb79-4097-8c69-e375fd1f42f4" />
