# 🌿 GreenGuard - Serre Autonome Intelligente

Bienvenue dans **GreenGuard**, un système de simulation IoT complet pour la gestion automatisée d'une serre.
Ce projet est conçu pour **Wokwi** et démontre une gestion multitâche complexe sur ESP32.

## 🌟 Fonctionnalités Implémentées

### 1. Contrôle Climatique (PID Simplifié)
*   **Chauffage** (LED Rouge + Relais) : S'active si Temp < 18°C.
*   **Ventilation** (Servo Moteur) : Le toit s'ouvre à 90° si Temp > 28°C.

### 2. Irrigation Intelligente
*   **Pompe** (LED Bleue + Relais) : S'active 5 secondes si Humidité < 40%.
*   *Protection* : Ne s'active pas si la pompe tourne déjà.

### 3. Cycle Lumineux
*   **Lampes de Croissance** (LED Magenta) : S'allument automatiquement s'il fait sombre (capteur LDR < 1000).

### 4. Sécurité Anti-Intrusion
*   **Détecteur Mouvement** (PIR) : Si un mouvement est détecté, une alerte est envoyée immédiatement (MQTT + Flash LCD).

### 5. Dashboard de Contrôle (Web)
*   Visualisation Temps Réel (Jauges & Graphiques).
*   **Mode Manuel** : Prenez le contrôle ! Forcez l'ouverture du toit ou l'arrosage depuis votre navigateur.

---

## 🚀 Comment Lancer la Simulation (Guide Pas à Pas)

### Étape 1 : Démarrer Wokwi
1.  Assurez-vous d'avoir l'extension **Wokwi Simulator** installée dans VS Code.
2.  Ouvrez le fichier `diagram.json`.
3.  Cliquez sur le bouton **Play** (Vert) en haut.
    *   *Le firmware va compiler et l'ESP32 va démarrer.*
    *   *L'écran LCD doit afficher "GreenGuard Init".*

### Étape 2 : Lancer le Dashboard
1.  Allez dans le dossier `web`.
2.  Ouvrez `index.html` dans votre navigateur (Chrome/Firefox).
3.  Attendez que le voyant passe au **Vert (Connecté MQTT)**.

### Étape 3 : Tester les Scénarios

#### 🌡️ Test Chauffage & Ventilation
1.  Dans Wokwi, cliquez sur le capteur **DHT22**.
2.  Baissez la température à **10°C** -> La **LED Rouge** (Chauffage) s'allume.
3.  Montez la température à **35°C** -> Le **Servo** bouge (Toit s'ouvre) et la LED Rouge s'éteint.

#### 💧 Test Arrosage
1.  Baissez l'humidité à **20%** -> La **LED Bleue** (Pompe) s'allume pendant 5 secondes puis s'éteint.

#### 🚨 Test Sécurité (Intrusion)
1.  Cliquez sur le capteur **PIR** (Carré blanc en bas) -> "Simulate Motion".
2.  Regardez votre Dashboard Web -> Une alerte "🚨 ALERTE SÉCURITÉ" apparaît !

#### 🎮 Test Contrôle Manuel
1.  Sur le Dashboard, cliquez sur **"CHANGER MODE"** pour passer en **MANUEL**.
2.  Cliquez sur **"Ouvrir Toit"** -> Le Servo bouge dans Wokwi instantanément.
3.  Cliquez sur **"Arroser"** -> La LED Bleue s'allume tant que vous ne cliquez pas sur "Stop".

---

## 🛠️ Configuration Technique
*   **MCU** : ESP32 DevKit V1
*   **Broker MQTT** : `broker.emqx.io` (Public)
*   **Port** : 1883 (ESP32) / 8083 (WebSockets)
*   **Topics** : `greenguard/data`, `greenguard/control`, `greenguard/alarm`

