var ws;
var reconnectInterval;
let deferredPrompt;

const translations = {
    'tr': {
        motorStatusLabel: 'Motor Durumu',
        completedTurnsLabel: 'Tamamlanan Turlar',
        turnsPerDayLabel: 'Günde Dönüş',
        turnDurationLabel: 'Dönüş Süresi',
        tabSettings: 'Ayarlar',
        tabWifi: 'WiFi Ayarları',
        tabOtherHorus: 'Diğer Horus Cihazları',
        tabTheme: 'Tema',
        tabLanguage: 'Dil',
        tabOta: 'Cihaz Güncelleme',
        settingsTitle: 'Ayarlar',
        turnsPerDayInputLabel: 'Günde Dönüş Sayısı',
        tourUnit: 'Tur',
        turnDurationInputLabel: 'Dönüş Süresi (saniye)',
        secondUnit: 's',
        directionLabel: 'Yön',
        directionForward: 'Sadece İleri',
        directionBackward: 'Sadece Geri',
        directionBoth: 'İleri ve Geri',
        startButton: 'Başlat',
        stopButton: 'Durdur',
        resetButton: 'Sıfırla',
        wifiSettingsTitle: 'WiFi Ayarları',
        networkNameLabel: 'Ağ Adı (SSID)',
        passwordLabel: 'Şifre',
        passwordPlaceholder: 'Şifrenizi buraya girin',
        saveAndRestartButton: 'Kaydet ve Yeniden Başlat',
        scanNetworksButton: 'Ağları Tara',
        otherHorusTitle: 'Diğer Horus Cihazları',
        mdnsNameLabel: 'MDNS Adı (Örn: horus-D99D)',
        mdnsNamePlaceholder: 'MDNS Adı (Örn: horus-D99D)',
        addButton: 'Ekle',
        themeTitle: 'Tema',
        themeSystem: 'Sistem',
        themeDark: 'Karanlık',
        themeLight: 'Aydınlık',
        languageTitle: 'Dil',
        languageSelectLabel: 'Arayüz Dili',
        otaTitle: 'Cihaz Güncelleme',
        currentVersionLabel: 'Güncel Sürüm',
        loading: 'Yükleniyor...',
        webInterfaceLabel: 'Web Arayüzü:',
        deviceNameLabel: 'Cihaz Adı',
        optionalDeviceNameLabel: 'Cihaz Adı (Opsiyonel)',
        deviceNamePlaceholder: 'Cihaz Adı',
        saveButton: 'Kaydet',
        checkUpdateButton: 'Güncelleme Kontrol Et',
        manualUpdateButton: 'Manuel Güncelleme',
        installAppButton: 'Uygulamayı Yükle',
        footerText: 'Caner Kocacık tarafından tasarlanmıştır.',
        motorStatusRunning: 'Çalışıyor',
        motorStatusStopped: 'Durduruldu',
        alertEnterMdns: 'Lütfen bir MDNS adı girin.',
        alertDeviceAdded: 'Cihaz eklendi: ',
        alertDeviceAddError: 'Cihaz eklenirken bir hata oluştu.',
        alertCommandSuccess: ' cihazı için komut başarıyla gönderildi.',
        alertConnectionError: ' cihazına bağlanılamadı.',
        scanningNetworks: 'Taranıyor...',
        otaChecking: 'Güncelleme kontrol ediliyor...',
        otaErrorConnect: 'Hata: Sunucuya bağlanılamadı.',
        noOtherDevices: 'Henüz başka cihaz eklenmemiş.'
    },
    'en': {
        motorStatusLabel: 'Motor Status',
        completedTurnsLabel: 'Completed Turns',
        turnsPerDayLabel: 'Turns Per Day',
        turnDurationLabel: 'Turn Duration',
        tabSettings: 'Settings',
        tabWifi: 'WiFi Settings',
        tabOtherHorus: 'Other Horus Devices',
        tabTheme: 'Theme',
        tabLanguage: 'Language',
        tabOta: 'Device Update',
        settingsTitle: 'Settings',
        turnsPerDayInputLabel: 'Turns Per Day',
        tourUnit: 'Turns',
        turnDurationInputLabel: 'Turn Duration (seconds)',
        secondUnit: 's',
        directionLabel: 'Direction',
        directionForward: 'Forward Only',
        directionBackward: 'Backward Only',
        directionBoth: 'Forward and Backward',
        startButton: 'Start',
        stopButton: 'Stop',
        resetButton: 'Reset',
        wifiSettingsTitle: 'WiFi Settings',
        networkNameLabel: 'Network Name (SSID)',
        passwordLabel: 'Password',
        passwordPlaceholder: 'Enter your password here',
        saveAndRestartButton: 'Save and Restart',
        scanNetworksButton: 'Scan Networks',
        otherHorusTitle: 'Other Horus Devices',
        mdnsNameLabel: 'MDNS Name (e.g., horus-D99D)',
        mdnsNamePlaceholder: 'MDNS Name (e.g., horus-D99D)',
        addButton: 'Add',
        themeTitle: 'Theme',
        themeSystem: 'System',
        themeDark: 'Dark',
        themeLight: 'Light',
        languageTitle: 'Language',
        languageSelectLabel: 'Interface Language',
        otaTitle: 'Device Update',
        currentVersionLabel: 'Current Version',
        loading: 'Loading...',
        webInterfaceLabel: 'Web Interface:',
        deviceNameLabel: 'Device Name',
        optionalDeviceNameLabel: 'Device Name (Optional)',
        deviceNamePlaceholder: 'Device Name',
        saveButton: 'Save',
        checkUpdateButton: 'Check for Updates',
        manualUpdateButton: 'Manual Update',
        installAppButton: 'Install App',
        footerText: 'Designed by Caner Kocacık.',
        motorStatusRunning: 'Running',
        motorStatusStopped: 'Stopped',
        alertEnterMdns: 'Please enter an MDNS name.',
        alertDeviceAdded: 'Device added: ',
        alertDeviceAddError: 'An error occurred while adding the device.',
        alertCommandSuccess: ' command sent successfully to ',
        alertConnectionError: 'Could not connect to ',
        scanningNetworks: 'Scanning...',
        otaChecking: 'Checking for update...',
        otaErrorConnect: 'Error: Could not connect to the server.',
        noOtherDevices: 'No other devices have been added yet.'
    },
    'de': {
        motorStatusLabel: 'Motorstatus',
        completedTurnsLabel: 'Abgeschlossene Umdrehungen',
        turnsPerDayLabel: 'Umdrehungen pro Tag',
        turnDurationLabel: 'Dauer der Umdrehung',
        tabSettings: 'Einstellungen',
        tabWifi: 'WLAN-Einstellungen',
        tabOtherHorus: 'Andere Horus-Geräte',
        tabTheme: 'Thema',
        tabLanguage: 'Sprache',
        tabOta: 'Geräte-Update',
        settingsTitle: 'Einstellungen',
        turnsPerDayInputLabel: 'Umdrehungen pro Tag',
        tourUnit: 'Umdr.',
        turnDurationInputLabel: 'Dauer der Umdrehung (Sekunden)',
        secondUnit: 's',
        directionLabel: 'Richtung',
        directionForward: 'Nur Vorwärts',
        directionBackward: 'Nur Rückwärts',
        directionBoth: 'Vorwärts und Rückwärts',
        startButton: 'Start',
        stopButton: 'Stopp',
        resetButton: 'Zurücksetzen',
        wifiSettingsTitle: 'WLAN-Einstellungen',
        networkNameLabel: 'Netzwerkname (SSID)',
        passwordLabel: 'Passwort',
        passwordPlaceholder: 'Geben Sie hier Ihr Passwort ein',
        saveAndRestartButton: 'Speichern und Neustarten',
        scanNetworksButton: 'Netzwerke suchen',
        otherHorusTitle: 'Andere Horus-Geräte',
        mdnsNameLabel: 'MDNS-Name (z.B. horus-D99D)',
        mdnsNamePlaceholder: 'MDNS-Name (z.B. horus-D99D)',
        addButton: 'Hinzufügen',
        themeTitle: 'Thema',
        themeSystem: 'System',
        themeDark: 'Dunkel',
        themeLight: 'Hell',
        languageTitle: 'Sprache',
        languageSelectLabel: 'Oberflächensprache',
        otaTitle: 'Geräte-Update',
        currentVersionLabel: 'Aktuelle Version',
        loading: 'Wird geladen...',
        webInterfaceLabel: 'Web-Oberfläche:',
        deviceNameLabel: 'Gerätename',
        optionalDeviceNameLabel: 'Gerätename (Optional)',
        deviceNamePlaceholder: 'Gerätename',
        saveButton: 'Speichern',
        checkUpdateButton: 'Nach Updates suchen',
        manualUpdateButton: 'Manuelles Update',
        installAppButton: 'App installieren',
        footerText: 'Entworfen von Caner Kocacık.',
        motorStatusRunning: 'Läuft',
        motorStatusStopped: 'Gestoppt',
        alertEnterMdns: 'Bitte geben Sie einen MDNS-Namen ein.',
        alertDeviceAdded: 'Gerät hinzugefügt: ',
        alertDeviceAddError: 'Beim Hinzufügen des Geräts ist ein Fehler aufgetreten.',
        alertCommandSuccess: ' Befehl erfolgreich an Gerät gesendet: ',
        alertConnectionError: 'Verbindung zum Gerät fehlgeschlagen: ',
        scanningNetworks: 'Scannen...',
        otaChecking: 'Suche nach Updates...',
        otaErrorConnect: 'Fehler: Verbindung zum Server konnte nicht hergestellt werden.',
        noOtherDevices: 'Es wurden noch keine anderen Geräte hinzugefügt.'
    },
    'fr': {
        motorStatusLabel: 'État du Moteur',
        completedTurnsLabel: 'Tours Terminés',
        turnsPerDayLabel: 'Tours par Jour',
        turnDurationLabel: 'Durée du Tour',
        tabSettings: 'Paramètres',
        tabWifi: 'Paramètres WiFi',
        tabOtherHorus: 'Autres Appareils Horus',
        tabTheme: 'Thème',
        tabLanguage: 'Langue',
        tabOta: 'Mise à Jour',
        settingsTitle: 'Paramètres',
        turnsPerDayInputLabel: 'Tours par Jour',
        tourUnit: 'Tours',
        turnDurationInputLabel: 'Durée du Tour (secondes)',
        secondUnit: 's',
        directionLabel: 'Direction',
        directionForward: 'Avant Seulement',
        directionBackward: 'Arrière Seulement',
        directionBoth: 'Avant et Arrière',
        startButton: 'Démarrer',
        stopButton: 'Arrêter',
        resetButton: 'Réinitialiser',
        wifiSettingsTitle: 'Paramètres WiFi',
        networkNameLabel: 'Nom du Réseau (SSID)',
        passwordLabel: 'Mot de passe',
        passwordPlaceholder: 'Entrez votre mot de passe ici',
        saveAndRestartButton: 'Enregistrer et Redémarrer',
        scanNetworksButton: 'Scanner les Réseaux',
        otherHorusTitle: 'Autres Appareils Horus',
        mdnsNameLabel: 'Nom MDNS (ex: horus-D99D)',
        mdnsNamePlaceholder: 'Nom MDNS (ex: horus-D99D)',
        addButton: 'Ajouter',
        themeTitle: 'Thème',
        themeSystem: 'Système',
        themeDark: 'Sombre',
        themeLight: 'Clair',
        languageTitle: 'Langue',
        languageSelectLabel: 'Langue de l\'interface',
        otaTitle: 'Mise à Jour de l\'appareil',
        currentVersionLabel: 'Version Actuelle',
        loading: 'Chargement...',
        webInterfaceLabel: 'Interface Web:',
        deviceNameLabel: 'Nom de l\'appareil',
        optionalDeviceNameLabel: 'Nom de l\'appareil (Optionnel)',
        deviceNamePlaceholder: 'Nom de l\'appareil',
        saveButton: 'Enregistrer',
        checkUpdateButton: 'Vérifier les Mises à Jour',
        manualUpdateButton: 'Mise à Jour Manuelle',
        installAppButton: 'Installer l\'Application',
        footerText: 'Conçu par Caner Kocacık.',
        motorStatusRunning: 'En marche',
        motorStatusStopped: 'Arrêté',
        alertEnterMdns: 'Veuillez entrer un nom MDNS.',
        alertDeviceAdded: 'Appareil ajouté : ',
        alertDeviceAddError: 'Une erreur s\'est produite lors de l\'ajout de l\'appareil.',
        alertCommandSuccess: ' commande envoyée avec succès à l\'appareil ',
        alertConnectionError: 'Impossible de se connecter à l\'appareil ',
        scanningNetworks: 'Balayage...',
        otaChecking: 'Vérification de la mise à jour...',
        otaErrorConnect: 'Erreur : Impossible de se connecter au serveur.',
        noOtherDevices: 'Aucun autre appareil n\'a encore été ajouté.'
    },
    'it': {
        motorStatusLabel: 'Stato Motore',
        completedTurnsLabel: 'Giri Completati',
        turnsPerDayLabel: 'Giri al Giorno',
        turnDurationLabel: 'Durata Giro',
        tabSettings: 'Impostazioni',
        tabWifi: 'Impostazioni WiFi',
        tabOtherHorus: 'Altri Dispositivi Horus',
        tabTheme: 'Tema',
        tabLanguage: 'Lingua',
        tabOta: 'Aggiornamento',
        settingsTitle: 'Impostazioni',
        turnsPerDayInputLabel: 'Giri al Giorno',
        tourUnit: 'Giri',
        turnDurationInputLabel: 'Durata Giro (secondi)',
        secondUnit: 's',
        directionLabel: 'Direzione',
        directionForward: 'Solo Avanti',
        directionBackward: 'Solo Indietro',
        directionBoth: 'Avanti e Indietro',
        startButton: 'Avvia',
        stopButton: 'Ferma',
        resetButton: 'Resetta',
        wifiSettingsTitle: 'Impostazioni WiFi',
        networkNameLabel: 'Nome Rete (SSID)',
        passwordLabel: 'Password',
        passwordPlaceholder: 'Inserisci qui la tua password',
        saveAndRestartButton: 'Salva e Riavvia',
        scanNetworksButton: 'Scansiona Reti',
        otherHorusTitle: 'Altri Dispositivi Horus',
        mdnsNameLabel: 'Nome MDNS (es: horus-D99D)',
        mdnsNamePlaceholder: 'Nome MDNS (es: horus-D99D)',
        addButton: 'Aggiungi',
        themeTitle: 'Tema',
        themeSystem: 'Sistema',
        themeDark: 'Scuro',
        themeLight: 'Chiaro',
        languageTitle: 'Lingua',
        languageSelectLabel: 'Lingua Interfaccia',
        otaTitle: 'Aggiornamento Dispositivo',
        currentVersionLabel: 'Versione Corrente',
        loading: 'Caricamento...',
        webInterfaceLabel: 'Interfaccia Web:',
        deviceNameLabel: 'Nome Dispositivo',
        optionalDeviceNameLabel: 'Nome Dispositivo (Opzionale)',
        deviceNamePlaceholder: 'Nome Dispositivo',
        saveButton: 'Salva',
        checkUpdateButton: 'Controlla Aggiornamenti',
        manualUpdateButton: 'Aggiornamento Manuale',
        installAppButton: 'Installa App',
        footerText: 'Progettato da Caner Kocacık.',
        motorStatusRunning: 'In funzione',
        motorStatusStopped: 'Fermato',
        alertEnterMdns: 'Inserisci un nome MDNS.',
        alertDeviceAdded: 'Dispositivo aggiunto: ',
        alertDeviceAddError: 'Si è verificato un errore durante l\'aggiunta del dispositivo.',
        alertCommandSuccess: ' comando inviato con successo al dispositivo ',
        alertConnectionError: 'Impossibile connettersi al dispositivo ',
        scanningNetworks: 'Scansione...',
        otaChecking: 'Controllo aggiornamenti...',
        otaErrorConnect: 'Errore: Impossibile connettersi al server.',
        noOtherDevices: 'Nessun altro dispositivo è stato ancora aggiunto.'
    },
    'es': {
        motorStatusLabel: 'Estado del Motor',
        completedTurnsLabel: 'Vueltas Completadas',
        turnsPerDayLabel: 'Vueltas por Día',
        turnDurationLabel: 'Duración de la Vuelta',
        tabSettings: 'Configuración',
        tabWifi: 'Configuración de WiFi',
        tabOtherHorus: 'Otros Dispositivos Horus',
        tabTheme: 'Tema',
        tabLanguage: 'Idioma',
        tabOta: 'Actualización',
        settingsTitle: 'Configuración',
        turnsPerDayInputLabel: 'Vueltas por Día',
        tourUnit: 'Vueltas',
        turnDurationInputLabel: 'Duración de la Vuelta (segundos)',
        secondUnit: 's',
        directionLabel: 'Dirección',
        directionForward: 'Solo Adelante',
        directionBackward: 'Solo Atrás',
        directionBoth: 'Adelante y Atrás',
        startButton: 'Iniciar',
        stopButton: 'Detener',
        resetButton: 'Reiniciar',
        wifiSettingsTitle: 'Configuración de WiFi',
        networkNameLabel: 'Nombre de Red (SSID)',
        passwordLabel: 'Contraseña',
        passwordPlaceholder: 'Ingrese su contraseña aquí',
        saveAndRestartButton: 'Guardar y Reiniciar',
        scanNetworksButton: 'Escanear Redes',
        otherHorusTitle: 'Otros Dispositivos Horus',
        mdnsNameLabel: 'Nombre MDNS (ej: horus-D99D)',
        mdnsNamePlaceholder: 'Nombre MDNS (ej: horus-D99D)',
        addButton: 'Añadir',
        themeTitle: 'Tema',
        themeSystem: 'Sistema',
        themeDark: 'Oscuro',
        themeLight: 'Claro',
        languageTitle: 'Idioma',
        languageSelectLabel: 'Idioma de la Interfaz',
        otaTitle: 'Actualización del Dispositivo',
        currentVersionLabel: 'Versión Actual',
        loading: 'Cargando...',
        webInterfaceLabel: 'Interfaz Web:',
        deviceNameLabel: 'Nombre del Dispositivo',
        optionalDeviceNameLabel: 'Nombre del Dispositivo (Opcional)',
        deviceNamePlaceholder: 'Nombre del Dispositivo',
        saveButton: 'Guardar',
        checkUpdateButton: 'Buscar Actualizaciones',
        manualUpdateButton: 'Actualización Manual',
        installAppButton: 'Instalar Aplicación',
        footerText: 'Diseñado por Caner Kocacık.',
        motorStatusRunning: 'Funcionando',
        motorStatusStopped: 'Detenido',
        alertEnterMdns: 'Por favor, ingrese un nombre MDNS.',
        alertDeviceAdded: 'Dispositivo añadido: ',
        alertDeviceAddError: 'Ocurrió un error al añadir el dispositivo.',
        alertCommandSuccess: ' comando enviado con éxito al dispositivo ',
        alertConnectionError: 'No se pudo conectar al dispositivo ',
        scanningNetworks: 'Escaneando...',
        otaChecking: 'Buscando actualizaciones...',
        otaErrorConnect: 'Error: No se pudo conectar al servidor.',
        noOtherDevices: 'Aún no se han agregado otros dispositivos.'
    },
    'zh-CN': {
        motorStatusLabel: '电机状态',
        completedTurnsLabel: '完成圈数',
        turnsPerDayLabel: '每日圈数',
        turnDurationLabel: '每圈时长',
        tabSettings: '设置',
        tabWifi: 'WiFi设置',
        tabOtherHorus: '其他Horus设备',
        tabTheme: '主题',
        tabLanguage: '语言',
        tabOta: '设备更新',
        settingsTitle: '设置',
        turnsPerDayInputLabel: '每日圈数',
        tourUnit: '圈',
        turnDurationInputLabel: '每圈时长 (秒)',
        secondUnit: '秒',
        directionLabel: '方向',
        directionForward: '仅向前',
        directionBackward: '仅向后',
        directionBoth: '向前和向后',
        startButton: '开始',
        stopButton: '停止',
        resetButton: '重置',
        wifiSettingsTitle: 'WiFi设置',
        networkNameLabel: '网络名称 (SSID)',
        passwordLabel: '密码',
        passwordPlaceholder: '在此输入您的密码',
        saveAndRestartButton: '保存并重启',
        scanNetworksButton: '扫描网络',
        otherHorusTitle: '其他Horus设备',
        mdnsNameLabel: 'MDNS名称 (例如: horus-D99D)',
        mdnsNamePlaceholder: 'MDNS名称 (例如: horus-D99D)',
        addButton: '添加',
        themeTitle: '主题',
        themeSystem: '系统',
        themeDark: '深色',
        themeLight: '浅色',
        languageTitle: '语言',
        languageSelectLabel: '界面语言',
        otaTitle: '设备更新',
        currentVersionLabel: '当前版本',
        loading: '加载中...',
        webInterfaceLabel: 'Web界面:',
        deviceNameLabel: '设备名称',
        optionalDeviceNameLabel: '设备名称 (可选)',
        deviceNamePlaceholder: '设备名称',
        saveButton: '保存',
        checkUpdateButton: '检查更新',
        manualUpdateButton: '手动更新',
        installAppButton: '安装应用',
        footerText: '由Caner Kocacık设计。',
        motorStatusRunning: '运行中',
        motorStatusStopped: '已停止',
        alertEnterMdns: '请输入MDNS名称。',
        alertDeviceAdded: '设备已添加: ',
        alertDeviceAddError: '添加设备时发生错误。',
        alertCommandSuccess: ' 命令已成功发送至设备 ',
        alertConnectionError: '无法连接到设备 ',
        scanningNetworks: '扫描中...',
        otaChecking: '正在检查更新...',
        otaErrorConnect: '错误：无法连接到服务器。',
        noOtherDevices: '尚未添加其他设备。'
    },
    'ja': {
        motorStatusLabel: 'モーター状態',
        completedTurnsLabel: '完了した回転数',
        turnsPerDayLabel: '1日の回転数',
        turnDurationLabel: '回転時間',
        tabSettings: '設定',
        tabWifi: 'WiFi設定',
        tabOtherHorus: '他のHorusデバイス',
        tabTheme: 'テーマ',
        tabLanguage: '言語',
        tabOta: 'デバイス更新',
        settingsTitle: '設定',
        turnsPerDayInputLabel: '1日の回転数',
        tourUnit: '回転',
        turnDurationInputLabel: '回転時間 (秒)',
        secondUnit: '秒',
        directionLabel: '方向',
        directionForward: '正転のみ',
        directionBackward: '逆転のみ',
        directionBoth: '正転と逆転',
        startButton: '開始',
        stopButton: '停止',
        resetButton: 'リセット',
        wifiSettingsTitle: 'WiFi設定',
        networkNameLabel: 'ネットワーク名 (SSID)',
        passwordLabel: 'パスワード',
        passwordPlaceholder: 'ここにパスワードを入力',
        saveAndRestartButton: '保存して再起動',
        scanNetworksButton: 'ネットワークをスキャン',
        otherHorusTitle: '他のHorusデバイス',
        mdnsNameLabel: 'MDNS名 (例: horus-D99D)',
        mdnsNamePlaceholder: 'MDNS名 (例: horus-D99D)',
        addButton: '追加',
        themeTitle: 'テーマ',
        themeSystem: 'システム',
        themeDark: 'ダーク',
        themeLight: 'ライト',
        languageTitle: '言語',
        languageSelectLabel: 'インターフェース言語',
        otaTitle: 'デバイスの更新',
        currentVersionLabel: '現在のバージョン',
        loading: '読み込み中...',
        webInterfaceLabel: 'Webインターフェース:',
        deviceNameLabel: 'デバイス名',
        optionalDeviceNameLabel: 'デバイス名 (任意)',
        deviceNamePlaceholder: 'デバイス名',
        saveButton: '保存',
        checkUpdateButton: '更新を確認',
        manualUpdateButton: '手動更新',
        installAppButton: 'アプリをインストール',
        footerText: 'Caner Kocacıkによるデザイン。',
        motorStatusRunning: '実行中',
        motorStatusStopped: '停止',
        alertEnterMdns: 'MDNS名を入力してください。',
        alertDeviceAdded: 'デバイスが追加されました: ',
        alertDeviceAddError: 'デバイスの追加中にエラーが発生しました。',
        alertCommandSuccess: ' コマンドがデバイスに正常に送信されました ',
        alertConnectionError: 'デバイスに接続できませんでした ',
        scanningNetworks: 'スキャン中...',
        otaChecking: '更新を確認しています...',
        otaErrorConnect: 'エラー：サーバーに接続できませんでした。',
        noOtherDevices: '他のデバイスはまだ追加されていません。'
    },
    'rm': {
        motorStatusLabel: 'Stadi dal motor',
        completedTurnsLabel: 'Rotaziuns terminadas',
        turnsPerDayLabel: 'Rotaziuns per di',
        turnDurationLabel: 'Durada da la rotaziun',
        tabSettings: 'Parameter',
        tabWifi: 'Parameter WLAN',
        tabOtherHorus: 'Auters apparats Horus',
        tabTheme: 'Tema',
        tabLanguage: 'Lingua',
        tabOta: 'Actualisaziun',
        settingsTitle: 'Parameter',
        turnsPerDayInputLabel: 'Rotaziuns per di',
        tourUnit: 'Rotaziuns',
        turnDurationInputLabel: 'Durada da la rotaziun (secundas)',
        secondUnit: 's',
        directionLabel: 'Direcziun',
        directionForward: 'Mo enavant',
        directionBackward: 'Mo enavos',
        directionBoth: 'Enavant ed enavos',
        startButton: 'Cumenzar',
        stopButton: 'Finir',
        resetButton: 'Resetar',
        wifiSettingsTitle: 'Parameter WLAN',
        networkNameLabel: 'Num da la rait (SSID)',
        passwordLabel: 'Pled-clav',
        passwordPlaceholder: 'Endatar il pled-clav',
        saveAndRestartButton: 'Memorisar e reaviar',
        scanNetworksButton: 'Tschertgar raits',
        otherHorusTitle: 'Auters apparats Horus',
        mdnsNameLabel: 'Num MDNS (p.ex. horus-D99D)',
        mdnsNamePlaceholder: 'Num MDNS (p.ex. horus-D99D)',
        addButton: 'Agiuntar',
        themeTitle: 'Tema',
        themeSystem: 'Sistem',
        themeDark: 'Stgir',
        themeLight: 'Cler',
        languageTitle: 'Lingua',
        languageSelectLabel: 'Lingua da l\'interfatscha',
        otaTitle: 'Actualisaziun da l\'apparat',
        currentVersionLabel: 'Versiun actuala',
        loading: 'Chargiar...',
        webInterfaceLabel: 'Interfatscha web:',
        deviceNameLabel: 'Num da l\'apparat',
        optionalDeviceNameLabel: 'Num da l\'apparat (opziunal)',
        deviceNamePlaceholder: 'Num da l\'apparat',
        saveButton: 'Memorisar',
        checkUpdateButton: 'Controllar actualisaziuns',
        manualUpdateButton: 'Actualisaziun manuala',
        installAppButton: 'Installar l\'applicaziun',
        footerText: 'Designà da Caner Kocacık.',
        motorStatusRunning: 'En funcziun',
        motorStatusStopped: 'Farmà',
        alertEnterMdns: 'Endatar in num MDNS, per plaschair.',
        alertDeviceAdded: 'Apparat agiuntà: ',
        alertDeviceAddError: 'Errur durant agiuntar l\'apparat.',
        alertCommandSuccess: ' Cumond tramess cun success a l\'apparat ',
        alertConnectionError: 'Na possì betg connectar a l\'apparat ',
        scanningNetworks: 'Tschertgar...',
        otaChecking: 'Controllar actualisaziuns...',
        otaErrorConnect: 'Errur: Na possì betg connectar al server.',
        noOtherDevices: 'Anc nagins auters apparats agiuntads.'
    },
    'mk': {
        motorStatusLabel: 'Статус на моторот',
        completedTurnsLabel: 'Завршени вртења',
        turnsPerDayLabel: 'Вртења на ден',
        turnDurationLabel: 'Времетраење на вртење',
        tabSettings: 'Поставки',
        tabWifi: 'WiFi Поставки',
        tabOtherHorus: 'Други Horus уреди',
        tabTheme: 'Тема',
        tabLanguage: 'Јазик',
        tabOta: 'Ажурирање',
        settingsTitle: 'Поставки',
        turnsPerDayInputLabel: 'Вртења на ден',
        tourUnit: 'Вртења',
        turnDurationInputLabel: 'Времетраење на вртење (секунди)',
        secondUnit: 's',
        directionLabel: 'Насока',
        directionForward: 'Само напред',
        directionBackward: 'Само назад',
        directionBoth: 'Напред и назад',
        startButton: 'Старт',
        stopButton: 'Стоп',
        resetButton: 'Ресетирај',
        wifiSettingsTitle: 'WiFi Поставки',
        networkNameLabel: 'Име на мрежа (SSID)',
        passwordLabel: 'Лозинка',
        passwordPlaceholder: 'Внесете ја вашата лозинка тука',
        saveAndRestartButton: 'Зачувај и рестартирај',
        scanNetworksButton: 'Скенирај мрежи',
        otherHorusTitle: 'Други Horus уреди',
        mdnsNameLabel: 'MDNS име (пр. horus-D99D)',
        mdnsNamePlaceholder: 'MDNS име (пр. horus-D99D)',
        addButton: 'Додај',
        themeTitle: 'Тема',
        themeSystem: 'Систем',
        themeDark: 'Темна',
        themeLight: 'Светла',
        languageTitle: 'Јазик',
        languageSelectLabel: 'Јазик на интерфејс',
        otaTitle: 'Ажурирање на уредот',
        currentVersionLabel: 'Моментална верзија',
        loading: 'Вчитување...',
        webInterfaceLabel: 'Веб интерфејс:',
        deviceNameLabel: 'Име на уред',
        optionalDeviceNameLabel: 'Име на уред (опционално)',
        deviceNamePlaceholder: 'Име на уред',
        saveButton: 'Зачувај',
        checkUpdateButton: 'Провери за ажурирања',
        manualUpdateButton: 'Рачно ажурирање',
        installAppButton: 'Инсталирај апликација',
        footerText: 'Дизајнирано од Caner Kocacık.',
        motorStatusRunning: 'Работи',
        motorStatusStopped: 'Запрен',
        alertEnterMdns: 'Ве молиме внесете MDNS име.',
        alertDeviceAdded: 'Уредот е додаден: ',
        alertDeviceAddError: 'Настана грешка при додавање на уредот.',
        alertCommandSuccess: ' командата е успешно испратена до уредот ',
        alertConnectionError: 'Не може да се поврзе со уредот ',
        scanningNetworks: 'Скенирање...',
        otaChecking: 'Проверка за ажурирање...',
        otaErrorConnect: 'Грешка: Не може да се поврзе со серверот.',
        noOtherDevices: 'Сè уште не се додадени други уреди.'
    },
    'sq': {
        motorStatusLabel: 'Statusi i Motorit',
        completedTurnsLabel: 'Rrotullime të Përfunduara',
        turnsPerDayLabel: 'Rrotullime në Ditë',
        turnDurationLabel: 'Kohëzgjatja e Rrotullimit',
        tabSettings: 'Cilësimet',
        tabWifi: 'Cilësimet e WiFi',
        tabOtherHorus: 'Pajisje të tjera Horus',
        tabTheme: 'Tema',
        tabLanguage: 'Gjuha',
        tabOta: 'Përditësimi',
        settingsTitle: 'Cilësimet',
        turnsPerDayInputLabel: 'Rrotullime në Ditë',
        tourUnit: 'Rrotullime',
        turnDurationInputLabel: 'Kohëzgjatja e Rrotullimit (sekonda)',
        secondUnit: 's',
        directionLabel: 'Drejtimi',
        directionForward: 'Vetëm Përpara',
        directionBackward: 'Vetëm Prapa',
        directionBoth: 'Përpara dhe Prapa',
        startButton: 'Nis',
        stopButton: 'Ndalo',
        resetButton: 'Rivendos',
        wifiSettingsTitle: 'Cilësimet e WiFi',
        networkNameLabel: 'Emri i Rrjetit (SSID)',
        passwordLabel: 'Fjalëkalimi',
        passwordPlaceholder: 'Shkruani fjalëkalimin tuaj këtu',
        saveAndRestartButton: 'Ruaj dhe Rinis',
        scanNetworksButton: 'Skano Rrjetet',
        otherHorusTitle: 'Pajisje të tjera Horus',
        mdnsNameLabel: 'Emri MDNS (p.sh. horus-D99D)',
        mdnsNamePlaceholder: 'Emri MDNS (p.sh. horus-D99D)',
        addButton: 'Shto',
        themeTitle: 'Tema',
        themeSystem: 'Sistemi',
        themeDark: 'E errët',
        themeLight: 'E çelët',
        languageTitle: 'Gjuha',
        languageSelectLabel: 'Gjuha e Ndërfaqes',
        otaTitle: 'Përditësimi i Pajisjes',
        currentVersionLabel: 'Versioni Aktual',
        loading: 'Duke u ngarkuar...',
        webInterfaceLabel: 'Ndërfaqja e Uebit:',
        deviceNameLabel: 'Emri i Pajisjes',
        optionalDeviceNameLabel: 'Emri i Pajisjes (Opsionale)',
        deviceNamePlaceholder: 'Emri i Pajisjes',
        saveButton: 'Ruaj',
        checkUpdateButton: 'Kontrollo për Përditësime',
        manualUpdateButton: 'Përditësim Manual',
        installAppButton: 'Instalo Aplikacionin',
        footerText: 'Projektuar nga Caner Kocacık.',
        motorStatusRunning: 'Në punë',
        motorStatusStopped: 'Ndalur',
        alertEnterMdns: 'Ju lutemi vendosni një emër MDNS.',
        alertDeviceAdded: 'Pajisja u shtua: ',
        alertDeviceAddError: 'Ndodhi një gabim gjatë shtimit të pajisjes.',
        alertCommandSuccess: ' komanda u dërgua me sukses te pajisja ',
        alertConnectionError: 'Nuk mund të lidhej me pajisjen ',
        scanningNetworks: 'Duke skanuar...',
        otaChecking: 'Duke kontrolluar për përditësim...',
        otaErrorConnect: 'Gabim: Nuk mund të lidhej me serverin.',
        noOtherDevices: 'Asnjë pajisje tjetër nuk është shtuar ende.'
    },
    'bs': {
        motorStatusLabel: 'Status Motora',
        completedTurnsLabel: 'Završeni Okreti',
        turnsPerDayLabel: 'Okreta Dnevno',
        turnDurationLabel: 'Trajanje Okreta',
        tabSettings: 'Postavke',
        tabWifi: 'WiFi Postavke',
        tabOtherHorus: 'Drugi Horus Uređaji',
        tabTheme: 'Tema',
        tabLanguage: 'Jezik',
        tabOta: 'Ažuriranje',
        settingsTitle: 'Postavke',
        turnsPerDayInputLabel: 'Okreta Dnevno',
        tourUnit: 'Okreta',
        turnDurationInputLabel: 'Trajanje Okreta (sekunde)',
        secondUnit: 's',
        directionLabel: 'Smjer',
        directionForward: 'Samo Naprijed',
        directionBackward: 'Samo Nazad',
        directionBoth: 'Naprijed i Nazad',
        startButton: 'Pokreni',
        stopButton: 'Zaustavi',
        resetButton: 'Resetuj',
        wifiSettingsTitle: 'WiFi Postavke',
        networkNameLabel: 'Naziv Mreže (SSID)',
        passwordLabel: 'Lozinka',
        passwordPlaceholder: 'Unesite vašu lozinku ovdje',
        saveAndRestartButton: 'Sačuvaj i Ponovo Pokreni',
        scanNetworksButton: 'Skeniraj Mreže',
        otherHorusTitle: 'Drugi Horus Uređaji',
        mdnsNameLabel: 'MDNS Naziv (npr. horus-D99D)',
        mdnsNamePlaceholder: 'MDNS Naziv (npr. horus-D99D)',
        addButton: 'Dodaj',
        themeTitle: 'Tema',
        themeSystem: 'Sistem',
        themeDark: 'Tamna',
        themeLight: 'Svijetla',
        languageTitle: 'Jezik',
        languageSelectLabel: 'Jezik Interfejsa',
        otaTitle: 'Ažuriranje Uređaja',
        currentVersionLabel: 'Trenutna Verzija',
        loading: 'Učitavanje...',
        webInterfaceLabel: 'Web Interfejs:',
        deviceNameLabel: 'Naziv Uređaja',
        optionalDeviceNameLabel: 'Naziv Uređaja (Opcionalno)',
        deviceNamePlaceholder: 'Naziv Uređaja',
        saveButton: 'Sačuvaj',
        checkUpdateButton: 'Provjeri Ažuriranja',
        manualUpdateButton: 'Ručno Ažuriranje',
        installAppButton: 'Instaliraj Aplikaciju',
        footerText: 'Dizajnirao Caner Kocacık.',
        motorStatusRunning: 'Radi',
        motorStatusStopped: 'Zaustavljen',
        alertEnterMdns: 'Molimo unesite MDNS naziv.',
        alertDeviceAdded: 'Uređaj dodan: ',
        alertDeviceAddError: 'Došlo je do greške prilikom dodavanja uređaja.',
        alertCommandSuccess: ' komanda uspješno poslana na uređaj ',
        alertConnectionError: 'Nije moguće povezati se na uređaj ',
        scanningNetworks: 'Skeniranje...',
        otaChecking: 'Provjera ažuriranja...',
        otaErrorConnect: 'Greška: Nije moguće povezati se na server.',
        noOtherDevices: 'Još uvijek nema dodanih drugih uređaja.'
    },
    'sr': {
        motorStatusLabel: 'Status Motora',
        completedTurnsLabel: 'Završeni Okreti',
        turnsPerDayLabel: 'Okreta Dnevno',
        turnDurationLabel: 'Trajanje Okreta',
        tabSettings: 'Podešavanja',
        tabWifi: 'WiFi Podešavanja',
        tabOtherHorus: 'Drugi Horus Uređaji',
        tabTheme: 'Tema',
        tabLanguage: 'Jezik',
        tabOta: 'Ažuriranje',
        settingsTitle: 'Podešavanja',
        turnsPerDayInputLabel: 'Okreta Dnevno',
        tourUnit: 'Okreta',
        turnDurationInputLabel: 'Trajanje Okreta (sekunde)',
        secondUnit: 's',
        directionLabel: 'Smer',
        directionForward: 'Samo Napred',
        directionBackward: 'Samo Nazad',
        directionBoth: 'Napred i Nazad',
        startButton: 'Pokreni',
        stopButton: 'Zaustavi',
        resetButton: 'Resetuj',
        wifiSettingsTitle: 'WiFi Podešavanja',
        networkNameLabel: 'Naziv Mreže (SSID)',
        passwordLabel: 'Lozinka',
        passwordPlaceholder: 'Unesite vašu lozinku ovde',
        saveAndRestartButton: 'Sačuvaj i Ponovo Pokreni',
        scanNetworksButton: 'Skeniraj Mreže',
        otherHorusTitle: 'Drugi Horus Uređaji',
        mdnsNameLabel: 'MDNS Naziv (npr. horus-D99D)',
        mdnsNamePlaceholder: 'MDNS Naziv (npr. horus-D99D)',
        addButton: 'Dodaj',
        themeTitle: 'Tema',
        themeSystem: 'Sistem',
        themeDark: 'Tamna',
        themeLight: 'Svetla',
        languageTitle: 'Jezik',
        languageSelectLabel: 'Jezik Interfejsa',
        otaTitle: 'Ažuriranje Uređaja',
        currentVersionLabel: 'Trenutna Verzija',
        loading: 'Učitavanje...',
        webInterfaceLabel: 'Web Interfejs:',
        deviceNameLabel: 'Naziv Uređaja',
        optionalDeviceNameLabel: 'Naziv Uređaja (Opciono)',
        deviceNamePlaceholder: 'Naziv Uređaja',
        saveButton: 'Sačuvaj',
        checkUpdateButton: 'Proveri Ažuriranja',
        manualUpdateButton: 'Ručno Ažuriranje',
        installAppButton: 'Instaliraj Aplikaciju',
        footerText: 'Dizajnirao Caner Kocacık.',
        motorStatusRunning: 'Radi',
        motorStatusStopped: 'Zaustavljen',
        alertEnterMdns: 'Molimo unesite MDNS naziv.',
        alertDeviceAdded: 'Uređaj dodat: ',
        alertDeviceAddError: 'Došlo je do greške prilikom dodavanja uređaja.',
        alertCommandSuccess: ' komanda uspešno poslata na uređaj ',
        alertConnectionError: 'Nije moguće povezati se na uređaj ',
        scanningNetworks: 'Skeniranje...',
        otaChecking: 'Provera ažuriranja...',
        otaErrorConnect: 'Greška: Nije moguće povezati se na server.',
        noOtherDevices: 'Još uvek nema dodatih drugih uređaja.'
    },
    'rup': {
        motorStatusLabel: 'Starea a Motorlui',
        completedTurnsLabel: 'Turnuri Completati',
        turnsPerDayLabel: 'Turnuri pi Zi',
        turnDurationLabel: 'Durata a Turnului',
        tabSettings: 'Pricădeanji',
        tabWifi: 'Pricădeanji WiFi',
        tabOtherHorus: 'Alti Horus Aparati',
        tabTheme: 'Tema',
        tabLanguage: 'Limba',
        tabOta: 'Noutati',
        settingsTitle: 'Pricădeanji',
        turnsPerDayInputLabel: 'Turnuri pi Zi',
        tourUnit: 'Turnuri',
        turnDurationInputLabel: 'Durata a Turnului (secundi)',
        secondUnit: 's',
        directionLabel: 'Direcția',
        directionForward: 'Ma Nainti',
        directionBackward: 'Ma Năpoi',
        directionBoth: 'Nainti și Năpoi',
        startButton: 'Apucă',
        stopButton: 'Astavă',
        resetButton: 'Dă Năpoi',
        wifiSettingsTitle: 'Pricădeanji WiFi',
        networkNameLabel: 'Numa a Rețelei (SSID)',
        passwordLabel: 'Parola',
        passwordPlaceholder: 'Bagă parola ta aclo',
        saveAndRestartButton: 'Scrie și Dă Năpoi',
        scanNetworksButton: 'Scaneadză Rețeli',
        otherHorusTitle: 'Alti Horus Aparati',
        mdnsNameLabel: 'Numa MDNS (ex: horus-D99D)',
        mdnsNamePlaceholder: 'Numa MDNS (ex: horus-D99D)',
        addButton: 'Adaugă',
        themeTitle: 'Tema',
        themeSystem: 'Sistem',
        themeDark: 'Neagră',
        themeLight: 'Albă',
        languageTitle: 'Limba',
        languageSelectLabel: 'Limba a Interfeței',
        otaTitle: 'Noutati a Aparatlui',
        currentVersionLabel: 'Versia di Tora',
        loading: 'Ncărcari...',
        webInterfaceLabel: 'Interfața Web:',
        deviceNameLabel: 'Numa a Aparatlui',
        optionalDeviceNameLabel: 'Numa a Aparatlui (Opțional)',
        deviceNamePlaceholder: 'Numa a Aparatlui',
        saveButton: 'Scrie',
        checkUpdateButton: 'Caftă Noutati',
        manualUpdateButton: 'Noutati Manuali',
        installAppButton: 'Instaleadză Aplicația',
        footerText: 'Făcut di Caner Kocacık.',
        motorStatusRunning: 'Meargi',
        motorStatusStopped: 'Astat',
        alertEnterMdns: 'Vă rugăm să introduceți un nume MDNS.',
        alertDeviceAdded: 'Aparatul adăugat: ',
        alertDeviceAddError: 'A apărut o eroare la adăugarea aparatului.',
        alertCommandSuccess: ' comanda trimisă cu succes la aparat ',
        alertConnectionError: 'Nu s-a putut conecta la aparat ',
        scanningNetworks: 'Scanari...',
        otaChecking: 'Căutari noutati...',
        otaErrorConnect: 'Eroare: Nu s-a putut conecta la server.',
        noOtherDevices: 'Nu s-au adăugat încă alte aparate.'
    },
    'rom': {
        motorStatusLabel: 'Motorosko Statuso',
        completedTurnsLabel: 'Pherde Vurti',
        turnsPerDayLabel: 'Vurti pe Dives',
        turnDurationLabel: 'Vurtako Vaxt',
        tabSettings: 'Postavke',
        tabWifi: 'WiFi Postavke',
        tabOtherHorus: 'Aver Horus Masinki',
        tabTheme: 'Tema',
        tabLanguage: 'Chhib',
        tabOta: 'Nevipe',
        settingsTitle: 'Postavke',
        turnsPerDayInputLabel: 'Vurti pe Dives',
        tourUnit: 'Vurti',
        turnDurationInputLabel: 'Vurtako Vaxt (sekunde)',
        secondUnit: 's',
        directionLabel: 'Righ',
        directionForward: 'Numa Anglal',
        directionBackward: 'Numa Palal',
        directionBoth: 'Anglal thaj Palal',
        startButton: 'Startuj',
        stopButton: 'Stopir',
        resetButton: 'Resetir',
        wifiSettingsTitle: 'WiFi Postavke',
        networkNameLabel: 'Mrezako Alav (SSID)',
        passwordLabel: 'Lozinka',
        passwordPlaceholder: 'Chiv to lozinka akate',
        saveAndRestartButton: 'Spasir thaj Restartir',
        scanNetworksButton: 'Skenir Mreze',
        otherHorusTitle: 'Aver Horus Masinki',
        mdnsNameLabel: 'MDNS Alav (npr. horus-D99D)',
        mdnsNamePlaceholder: 'MDNS Alav (npr. horus-D99D)',
        addButton: 'Dodajin',
        themeTitle: 'Tema',
        themeSystem: 'Sistem',
        themeDark: 'Kalo',
        themeLight: 'Parhno',
        languageTitle: 'Chhib',
        languageSelectLabel: 'Interfejsosko Chhib',
        otaTitle: 'Masinkako Nevipe',
        currentVersionLabel: 'Akutno Verzija',
        loading: 'Ladiripe...',
        webInterfaceLabel: 'Web Interfejso:',
        deviceNameLabel: 'Masinkako Alav',
        optionalDeviceNameLabel: 'Masinkako Alav (Opcionalno)',
        deviceNamePlaceholder: 'Masinkako Alav',
        saveButton: 'Spasir',
        checkUpdateButton: 'Dikh Nevipe',
        manualUpdateButton: 'Manuelno Nevipe',
        installAppButton: 'Instalir Aplikacija',
        footerText: 'Kerdo katar Caner Kocacık.',
        motorStatusRunning: 'Phirel',
        motorStatusStopped: 'Stopirime',
        alertEnterMdns: 'Molimo te, chiv MDNS alav.',
        alertDeviceAdded: 'Masinka dodajime: ',
        alertDeviceAddError: 'Greska ked dodajisarda masinka.',
        alertCommandSuccess: ' komanda uspjesno bichhaldi ki masinka ',
        alertConnectionError: 'Nashti povezime pe masinka ',
        scanningNetworks: 'Skeniripe...',
        otaChecking: 'Dikhipe nevipasko...',
        otaErrorConnect: 'Greska: Nashti povezime pe servero.',
        noOtherDevices: 'Nane aver masinki dodajime.'
    },
    'az': {
        motorStatusLabel: 'Motor Vəziyyəti',
        completedTurnsLabel: 'Tamamlanmış Dövrələr',
        turnsPerDayLabel: 'Gündəlik Dövrə',
        turnDurationLabel: 'Dövrə Müddəti',
        tabSettings: 'Parametrlər',
        tabWifi: 'WiFi Parametrləri',
        tabOtherHorus: 'Digər Horus Cihazları',
        tabTheme: 'Mövzu',
        tabLanguage: 'Dil',
        tabOta: 'Cihaz Yeniləməsi',
        settingsTitle: 'Parametrlər',
        turnsPerDayInputLabel: 'Gündəlik Dövrə Sayı',
        tourUnit: 'Dövrə',
        turnDurationInputLabel: 'Dövrə Müddəti (saniyə)',
        secondUnit: 'san',
        directionLabel: 'İstiqamət',
        directionForward: 'Yalnız İrəli',
        directionBackward: 'Yalnız Geri',
        directionBoth: 'İrəli və Geri',
        startButton: 'Başlat',
        stopButton: 'Dayandır',
        resetButton: 'Sıfırla',
        wifiSettingsTitle: 'WiFi Parametrləri',
        networkNameLabel: 'Şəbəkə Adı (SSID)',
        passwordLabel: 'Şifrə',
        passwordPlaceholder: 'Şifrənizi bura daxil edin',
        saveAndRestartButton: 'Yadda Saxla və Yenidən Başlat',
        scanNetworksButton: 'Şəbəkələri Tara',
        otherHorusTitle: 'Digər Horus Cihazları',
        mdnsNameLabel: 'MDNS Adı (məs: horus-D99D)',
        mdnsNamePlaceholder: 'MDNS Adı (məs: horus-D99D)',
        addButton: 'Əlavə et',
        themeTitle: 'Mövzu',
        themeSystem: 'Sistem',
        themeDark: 'Tünd',
        themeLight: 'Açıq',
        languageTitle: 'Dil',
        languageSelectLabel: 'İnterfeys Dili',
        otaTitle: 'Cihaz Yeniləməsi',
        currentVersionLabel: 'Mövcud Versiya',
        loading: 'Yüklənir...',
        webInterfaceLabel: 'Veb İnterfeysi:',
        deviceNameLabel: 'Cihaz Adı',
        optionalDeviceNameLabel: 'Cihaz Adı (İstəyə Bağlı)',
        deviceNamePlaceholder: 'Cihaz Adı',
        saveButton: 'Yadda Saxla',
        checkUpdateButton: 'Yeniləmələri Yoxla',
        manualUpdateButton: 'Əl ilə Yeniləmə',
        installAppButton: 'Tətbiqi Yüklə',
        footerText: 'Caner Kocacık tərəfindən hazırlanmışdır.',
        motorStatusRunning: 'İşləyir',
        motorStatusStopped: 'Dayandırılıb',
        alertEnterMdns: 'Zəhmət olmasa bir MDNS adı daxil edin.',
        alertDeviceAdded: 'Cihaz əlavə edildi: ',
        alertDeviceAddError: 'Cihaz əlavə edilərkən xəta baş verdi.',
        alertCommandSuccess: ' əmri cihaza uğurla göndərildi ',
        alertConnectionError: 'Cihaza qoşulmaq mümkün olmadı ',
        scanningNetworks: 'Taranır...',
        otaChecking: 'Yeniləmə yoxlanılır...',
        otaErrorConnect: 'Xəta: Serverə qoşulmaq mümkün olmadı.',
        noOtherDevices: 'Hələ başqa cihaz əlavə edilməyib.'
    },
    'ru': {
         motorStatusLabel: 'Статус Мотора',
         completedTurnsLabel: 'Завершенные Обороты',
         turnsPerDayLabel: 'Оборотов в день',
         turnDurationLabel: 'Длительность Оборота',
         tabSettings: 'Настройки',
         tabWifi: 'Настройки WiFi',
         tabOtherHorus: 'Другие Horus',
         tabTheme: 'Тема',
         tabLanguage: 'Язык',
         tabOta: 'Обновление',
         settingsTitle: 'Настройки',
         turnsPerDayInputLabel: 'Оборотов в день',
         tourUnit: 'оборотов',
         turnDurationInputLabel: 'Длительность оборота (секунды)',
         secondUnit: 'с',
         directionLabel: 'Направление',
         directionForward: 'Только вперед',
         directionBackward: 'Только назад',
         directionBoth: 'Вперед и назад',
         startButton: 'Старт',
         stopButton: 'Стоп',
         resetButton: 'Сброс',
         wifiSettingsTitle: 'Настройки WiFi',
         networkNameLabel: 'Имя сети (SSID)',
         passwordLabel: 'Пароль',
         passwordPlaceholder: 'Введите ваш пароль',
         saveAndRestartButton: 'Сохранить и перезапустить',
         scanNetworksButton: 'Сканировать сети',
         otherHorusTitle: 'Другие устройства Horus',
         mdnsNameLabel: 'Имя MDNS (пример: horus-D99D)',
         mdnsNamePlaceholder: 'Имя MDNS (пример: horus-D99D)',
         addButton: 'Добавить',
         themeTitle: 'Тема',
         themeSystem: 'Системная',
         themeDark: 'Темная',
         themeLight: 'Светлая',
         languageTitle: 'Язык',
         languageSelectLabel: 'Язык интерфейса',
         otaTitle: 'Обновление Устройства',
         currentVersionLabel: 'Текущая Версия',
         loading: 'Загрузка...',
         webInterfaceLabel: 'Веб-интерфейс:',
         deviceNameLabel: 'Имя Устройства',
         optionalDeviceNameLabel: 'Имя устройства (необязательно)',
         deviceNamePlaceholder: 'Имя Устройства',
         saveButton: 'Сохранить',
         checkUpdateButton: 'Проверить обновления',
         manualUpdateButton: 'Ручное Обновление',
         installAppButton: 'Установить Приложение',
         footerText: 'Разработано Caner Kocacık.',
         motorStatusRunning: 'Работает',
         motorStatusStopped: 'Остановлен',
         alertEnterMdns: 'Пожалуйста, введите имя MDNS.',
         alertDeviceAdded: 'Устройство добавлено: ',
         alertDeviceAddError: 'Произошла ошибка при добавлении устройства.',
         alertCommandSuccess: ' команда успешно отправлена на устройство ',
         alertConnectionError: 'Не удалось подключиться к устройству ',
         scanningNetworks: 'Сканирование...',
         otaChecking: 'Проверка обновлений...',
         otaErrorConnect: 'Ошибка: Не удалось подключиться к серверу.',
         noOtherDevices: 'Другие устройства еще не добавлены.'
    }
};

function setLanguage(lang) {
    const langStrings = translations[lang] || translations['tr'];
    document.querySelectorAll('[data-lang-key]').forEach(elem => {
        const key = elem.getAttribute('data-lang-key');
        if (langStrings[key]) {
            if (elem.placeholder !== undefined) {
                elem.placeholder = langStrings[key];
            } else {
                elem.innerText = langStrings[key];
            }
        }
    });
    localStorage.setItem('language', lang);
    document.documentElement.lang = lang;
}

function loadLanguage() {
    const savedLang = localStorage.getItem('language') || 'tr';
    document.getElementById('languageSelect').value = savedLang;
    setLanguage(savedLang);
}

function getTranslation(key, lang = localStorage.getItem('language') || 'tr') {
    return (translations[lang] && translations[lang][key]) || translations['tr'][key];
}

function setTheme(theme) {
    if (theme === 'dark') {
        document.body.classList.remove('light');
        localStorage.setItem('theme', 'dark');
    } else if (theme === 'light') {
        document.body.classList.add('light');
        localStorage.setItem('theme', 'light');
    } else {
        if (window.matchMedia('(prefers-color-scheme: light)').matches) {
            document.body.classList.add('light');
        } else {
            document.body.classList.remove('light');
        }
        localStorage.removeItem('theme');
    }
}

function loadTheme() {
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme) {
        document.getElementById('theme' + savedTheme.charAt(0).toUpperCase() + savedTheme.slice(1)).checked = true;
        setTheme(savedTheme);
    } else {
        document.getElementById('themeSystem').checked = true;
        setTheme('system');
    }
}

function showTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
    document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));
    document.getElementById(tabId + 'Tab').classList.add('active');
    document.getElementById('tab-' + tabId).classList.add('active');
}

function handleMessage(data) {
    try {
        var doc = JSON.parse(data);
        if (doc.tpd) {
            document.getElementById('turnsPerDay').innerText = doc.tpd;
            document.getElementById('turnsPerDayValue').innerText = doc.tpd;
            document.getElementById('turnsPerDayInput').value = doc.tpd;
        }
        if (doc.duration) {
            document.getElementById('turnDuration').innerText = doc.duration + ' ' + getTranslation('secondUnit');
            document.getElementById('turnDurationValue').innerText = doc.duration;
            document.getElementById('turnDurationInput').value = doc.duration;
        }
        if (doc.direction) {
            const radio = document.getElementById('direction' + doc.direction);
            if (radio) radio.checked = true;
        }
        if (doc.customName && doc.customName.length > 0) {
            document.getElementById('deviceName').innerText = doc.customName;
            document.getElementById('nameInput').value = doc.customName;
            document.getElementById('nameInput').placeholder = '';
        } else {
            document.getElementById('deviceName').innerText = doc.mDNSHostname;
            document.getElementById('nameInput').value = '';
            document.getElementById('nameInput').placeholder = doc.mDNSHostname;
        }
        if (doc.ip && doc.mDNSHostname) {
            document.getElementById('ipAddress').innerHTML = doc.ip + '<br><small style="font-size: 0.8em;">' + doc.mDNSHostname + '.local</small>';
        }
        if (doc.motorStatus) {
            let statusKey = 'motorStatusStopped';
            if (doc.motorStatus.includes('Çalışıyor') || doc.motorStatus.includes('çalışıyor')) {
                statusKey = 'motorStatusRunning';
            }
            document.getElementById('motorStatus').innerText = getTranslation(statusKey);
        }
        if (doc.completedTurns) document.getElementById('completedTurns').innerText = doc.completedTurns;
        if (doc.version) document.getElementById('currentVersion').innerText = doc.version;
        if (doc.otaStatus) {
            const msgBox = document.getElementById('message_box');
            msgBox.innerText = doc.otaStatus;
            msgBox.style.color = (doc.otaStatus.includes('Hata') || doc.otaStatus.includes('başarısız')) ? 'red' : 'green';
            msgBox.style.display = 'block';
            if (!doc.otaStatus.includes("indiriliyor")) {
                setTimeout(() => { msgBox.style.display = 'none'; }, 5000);
            }
        }
        if (doc.otherHorus) {
            renderOtherHorusList(doc.otherHorus);
        }
    } catch(e) {
        console.error("JSON parse error:", e);
    }
}

function connectWebSocket() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        return;
    }
    ws = new WebSocket('ws://' + window.location.hostname + ':81/');
    ws.onopen = function() {
        console.log('WebSocket connection opened.');
        clearInterval(reconnectInterval);
        ws.send('status_request');
    };
    ws.onmessage = function(event) {
        console.log('Message received:', event.data);
        handleMessage(event.data);
    };
    ws.onclose = function() {
        console.log('WebSocket connection closed, reconnecting...');
        if (!reconnectInterval) {
            reconnectInterval = setInterval(connectWebSocket, 5000);
        }
    };
    ws.onerror = function(error) {
        console.error('WebSocket error:', error);
        ws.close();
    };
}

function renderOtherHorusList(devices) {
    const listContainer = document.getElementById('otherHorusList');
    listContainer.innerHTML = '';
    if (devices.length === 0) {
        listContainer.innerHTML = `<p style="text-align: center; color: var(--secondary-color);">${getTranslation('noOtherDevices')}</p>`;
        return;
    }
    devices.forEach(device => {
        const item = document.createElement('div');
        item.className = 'other-horus-item';
        item.innerHTML = `
            <span>${device}.local</span>
            <div class="button-group">
                <button class="button primary" onclick="controlOtherHorus('${device}', 'start')">${getTranslation('startButton')}</button>
                <button class="button secondary" onclick="controlOtherHorus('${device}', 'stop')">${getTranslation('stopButton')}</button>
                <button class="button secondary" onclick="controlOtherHorus('${device}', 'reset')">${getTranslation('resetButton')}</button>
            </div>
        `;
        listContainer.appendChild(item);
    });
}

function addOtherHorus() {
    const mdnsName = document.getElementById('otherHorusName').value;
    if (!mdnsName) {
        alert(getTranslation('alertEnterMdns'));
        return;
    }
    fetch('/add_other_horus', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `mdns_name=${encodeURIComponent(mdnsName)}`
    })
    .then(response => response.text())
    .then(data => {
        console.log("Add device response:", data);
        alert(getTranslation('alertDeviceAdded') + mdnsName);
        document.getElementById('otherHorusName').value = "";
        requestStatusUpdate();
    })
    .catch(error => {
        console.error('Add device error:', error);
        alert(getTranslation('alertDeviceAddError'));
    });
}

function controlOtherHorus(mdnsName, action) {
    console.log(`Sending ${action} command to ${mdnsName}...`);
    fetch(`http://${mdnsName}.local/set?action=${action}`)
        .then(response => response.text())
        .then(data => {
            console.log(`Response from ${mdnsName}:`, data);
            alert(`${mdnsName}.local: ${getTranslation('alertCommandSuccess')}`);
        })
        .catch(error => {
            console.error(`Control device (${mdnsName}) error:`, error);
            alert(`${getTranslation('alertConnectionError')} ${mdnsName}.local.`);
        });
}

function sendSettings(action) {
    const turnsPerDay = document.getElementById('turnsPerDayInput').value;
    const turnDuration = document.getElementById('turnDurationInput').value;
    const direction = document.querySelector('input[name="direction"]:checked').value;
    let url = `/set?tpd=${turnsPerDay}&duration=${turnDuration}&dir=${direction}`;
    if (action) {
        url += `&action=${action}`;
    }
    fetch(url)
        .then(response => response.text())
        .then(data => {
            console.log(data);
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send('status_request');
            }
        })
        .catch(error => console.error('Error:', error));
}

function requestStatusUpdate() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('status_request');
    }
}

function saveWiFiSettings() {
    const ssid = document.getElementById('ssidSelect').value;
    const password = document.getElementById('passwordInput').value;
    fetch('/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
    })
    .then(response => response.text())
    .then(data => {
        console.log(data);
    })
    .catch(error => console.error('Error:', error));
}

function saveDeviceName() {
    const name = document.getElementById('nameInput').value;
    fetch('/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `name=${encodeURIComponent(name)}`
    })
    .then(response => response.text())
    .then(data => {
        console.log(data);
    })
    .catch(error => console.error('Error:', error));
}

function resetDeviceName() {
    document.getElementById('nameInput').value = "";
    saveDeviceName();
}

function scanNetworks() {
    const scanButton = document.querySelector('#wifiTab .button.secondary');
    const originalText = scanButton.innerText;
    scanButton.innerText = getTranslation('scanningNetworks');
    scanButton.disabled = true;
    fetch('/scan')
        .then(response => response.json()) // Changed to response.json()
        .then(data => {
            const select = document.getElementById('ssidSelect');
            select.innerHTML = ''; // Clear previous options
            if (data.networks) {
                data.networks.forEach(net => {
                    const option = document.createElement('option');
                    option.value = net.ssid;
                    // BSSID is not in the JSON, but you can add it if you want
                    option.innerText = `${net.ssid} (${net.rssi})`;
                    select.appendChild(option);
                });
            }
            scanButton.innerText = originalText;
            scanButton.disabled = false;
        })
        .catch(error => {
            console.error('Error:', error);
            scanButton.innerText = originalText;
            scanButton.disabled = false;
        });
}

function checkOTAUpdate() {
    const msgBox = document.getElementById('message_box');
    msgBox.innerText = getTranslation('otaChecking');
    msgBox.style.color = 'yellow';
    msgBox.style.display = 'block';
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('ota_check_request');
    } else {
        msgBox.innerText = getTranslation('otaErrorConnect');
        msgBox.style.color = 'red';
        setTimeout(() => { msgBox.style.display = 'none'; }, 5000);
    }
}

document.querySelectorAll('input[name="theme"]').forEach(radio => {
    radio.addEventListener('change', (event) => {
        setTheme(event.target.value);
    });
});
window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault();
    deferredPrompt = e;
    document.getElementById('pwa_install_button').style.display = 'block';
    console.log('Install prompt ready');
});
window.addEventListener('appinstalled', () => {
    console.log('PWA installed successfully.');
    document.getElementById('pwa_install_button').style.display = 'none';
});
document.getElementById('pwa_install_button').addEventListener('click', async () => {
    if (deferredPrompt) {
        deferredPrompt.prompt();
        const { outcome } = await deferredPrompt.userChoice;
        console.log(outcome === 'accepted' ? 'User accepted install' : 'User dismissed install');
        deferredPrompt = null;
        document.getElementById('pwa_install_button').style.display = 'none';
    }
});
window.onload = function() {
    loadTheme();
    loadLanguage();
    connectWebSocket();
    showTab('settings');
    if ('serviceWorker' in navigator) {
        navigator.serviceWorker.register('/sw.js').then((reg) => {
            console.log('Service Worker registered:', reg);
        }).catch((error) => {
            console.error('Service Worker registration failed:', error);
        });
    }
};

function uploadFirmware() {
    let fileInput = document.getElementById('firmwareFile');
    if (fileInput.files.length === 0) {
        document.getElementById('message_box').innerText = 'Lütfen bir dosya seçin.';
        return;
    }
    let formData = new FormData();
    formData.append('firmware', fileInput.files[0]);
    fetch('/manual_update', { method: 'POST', body: formData })
    .then(response => response.text())
    .then(data => {
        document.getElementById('message_box').innerText = 'Güncelleme gönderildi. Cihaz yeniden başlatılıyor...';
        document.getElementById('message_box').className = 'success';
    })
    .catch(error => {
        document.getElementById('message_box').innerText = 'Güncelleme gönderilirken hata oluştu.';
        document.getElementById('message_box').className = 'error';
    });

}

