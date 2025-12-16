# How to Run GreenGuard Simulation on Wokwi.com

If the local VS Code extension is failing with a "Service Worker" error, you can run the simulation directly in your browser.

1. Open [Wokwi ESP32 Project](https://wokwi.com/projects/new/esp32)
2. **Library Setup**:
   - Click on the "Library Manager" (or `libraries.txt` tab).
   - Add the following libraries:
     ```text
     PubSubClient
     DHT sensor library
     Adafruit Unified Sensor
     LiquidCrystal_I2C
     ESP32Servo
     ArduinoJson
     ```

3. **Code Setup**:
   - Copy the content of `src/main.cpp` from this project.
   - Paste it into the `sketch.ino` tab in Wokwi.

4. **Diagram Setup**:
   - Copy the content of `diagram.json` from this project.
   - Paste it into the `diagram.json` tab in Wokwi.

5. **Run**:
   - Click the Green Play button.

## Troubleshooting Local VS Code Extension
The error `InvalidStateError: Failed to register a ServiceWorker` is usually a temporary VS Code glitch.
- Try **Restarting VS Code** completely.
- If that doesn't work, run the command `Developer: Reload Window` from the Command Palette (`Ctrl+Shift+P`).
