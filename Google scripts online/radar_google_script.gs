// --- TESTFUNKTION FÜR DEN EDITOR ---
function testCreate2hPdf() {
  var ss = SpreadsheetApp.openById(getSpreadsheetId());
  var result = createTwoHourSummaryPdf(ss, new Date(), "testuser");
  Logger.log(result);
}
// --- KONFIGURATION ---
const SECRET_GOOGLE_URL = "https://script.google.com/macros/s/AKfycbyy7ObOq7-s_7rDwxNg7j-HbNpKyMWyx3Q-eeycHd0lKNUD2zV31vV2yFOW4xmNZQQo/exec";
const DEFAULT_SPREADSHEET_ID = "1_9LXDZUw0UWPutHtHIrgWSbpcrgp2U3R3xJRxGeRfGQ";
const DATA_SHEET_NAME = "Messungen";
const DAILY_SHEET_PREFIX = "Messungen_";
const USER_SHEET_NAME = "Nutzer";
const RADAR_CONFIG_SHEET_NAME = "RadarConfig";
const DRIVE_ROOT_FOLDER_ID = "1h5YkXeq3YcGe6gRqLClrvDPrHFCL7rQd"; // Deine Drive-Ordner-ID
const TWO_HOUR_PDF_PREFIX = "Radar-2h-Bericht-";
const DATA_HEADERS = ["Zeitstempel", "Geschwindigkeit", "Limit", "Überschreitung", "Status", "Quelle"];
const WEEKDAY_NAMES_DE = ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"];
const DEPLOY_MARKER = "READY_V55_RADAR_MARKER";

function getSpreadsheetId() {
  return (typeof SPREADSHEET_ID !== "undefined" && SPREADSHEET_ID)
    ? SPREADSHEET_ID
    : DEFAULT_SPREADSHEET_ID;
}

function getMeasureSheetName() {
  return (typeof MEASURE_SHEET !== "undefined" && MEASURE_SHEET)
    ? MEASURE_SHEET
    : DATA_SHEET_NAME;
}

function getUserSheetName() {
  return (typeof USER_SHEET !== "undefined" && USER_SHEET)
    ? USER_SHEET
    : USER_SHEET_NAME;
}

function getScriptTimeZone() {
  return (typeof TZ !== "undefined" && TZ)
    ? TZ
    : Session.getScriptTimeZone();
}

/**
 * 1. SETUP-FUNKTION
 * Führe diese Funktion im Editor einmal manuell aus (oben auswählen und auf 'Ausführen' klicken),
 * um die Tabellenstruktur anzulegen.
 */
function setup() {
  const ss = SpreadsheetApp.openById(getSpreadsheetId());
  const measureSheetName = getMeasureSheetName();
  const userSheetName = getUserSheetName();

  // Tagesblatt für Messungen erstellen
  getOrCreateDailyDataSheet(ss, new Date());

  // Optionales Legacy-Blatt erhalten, falls schon vorhanden
  if (!ss.getSheetByName(measureSheetName)) {
    const legacySheet = ss.insertSheet(measureSheetName);
    legacySheet.appendRow(DATA_HEADERS);
    legacySheet.setFrozenRows(1);
  } else {
    ss.getSheetByName(measureSheetName).setFrozenRows(1);
  }

  // Blatt für Nutzer erstellen
  if (!ss.getSheetByName(userSheetName)) {
    const userSheet = ss.insertSheet(userSheetName);
    userSheet.appendRow(["id", "allowed", "role", "email", "name"]);
    userSheet.setFrozenRows(1);
  } else {
    ss.getSheetByName(userSheetName).setFrozenRows(1);
  }

  getOrCreateRadarConfigSheet(ss);

  console.log("Setup abgeschlossen. Tabellen sind bereit.");
}

/**
 * 2. HAUPTSCHNITTSTELLE (doGet)
 * Wird bei jeder Web-Anfrage (GET) vom ESP32 aufgerufen.
 */
function doGet(e) {
  const p = (e && e.parameter) ? e.parameter : {};
  const action = p.action; // Parameter für Aktionen wie "GET_STATS"
  const speed = p.speed;   // Parameter für Geschwindigkeitsmessungen
  const event = p.event;
  const ss = SpreadsheetApp.openById(getSpreadsheetId());

  // Fall 0: Legacy-Event (Display sendet event=...)
  if (event) {
    return ContentService.createTextOutput(handleLegacyEvent(ss, event, p));
  }

  // Fall 1: Eine Aktion wird ausgeführt (von Telegram ausgelöst)
  if (action) {
    switch (action) {
      case "GET_STATS":
        if (typeof generateDailyStatsReport === "function") {
          return ContentService.createTextOutput(generateDailyStatsReport(ss));
        }
        return ContentService.createTextOutput(generateDailyStats(ss));

      case "GENERATE_DAILY_PDF": {
        const requesterId = p.id || "unknown";
        const pdf = createDailySummaryPdf(ss, new Date(), requesterId);
        return ContentService.createTextOutput(pdf.url);
      }

      case "GENERATE_2H_PDF": {
        const requesterId = p.id || "unknown";
        const pdf = createTwoHourSummaryPdf(ss, new Date(), requesterId);
        return ContentService.createTextOutput(pdf.url);
      }

      case "ADD_TEST_ENTRY": {
        const speedValue = parseInt(p.speed || "1", 10) || 1;
        const limitValue = parseInt(p.limit || "50", 10) || 50;
        const requesterId = p.id || "unknown";
        const saved = appendMeasurementRow(ss, new Date(), speedValue, limitValue, "TEST", "TEST");
        return ContentService.createTextOutput("OK TEST " + speedValue + " km/h | " + saved.sheetName + " | id=" + requesterId);
      }

      case "SEND_EMAIL":
        const userId = p.id;
        const userEmail = getUserEmail(ss, userId);
        if (userEmail) {
          sendReport(userEmail, ss);
          return ContentService.createTextOutput("✅ Report an " + userEmail + " versendet.");
        } else {
          return ContentService.createTextOutput("❌ Keine E-Mail für ID " + userId + " gefunden.");
        }

      case "GET_USERS":
        const data = ss.getSheetByName(getUserSheetName()).getDataRange().getValues();
        const users = [];
        // Start bei 1, um die Kopfzeile zu überspringen
        for (let i = 1; i < data.length; i++) {
          users.push({
            id: data[i][0],
            allowed: data[i][1] === true || data[i][1] === "true",
            role: data[i][2]
          });
        }
        return JSONResponse(users);

      case "GET_LAST":
        return ContentService.createTextOutput(getLastMeasurementAsJson(ss)).setMimeType(ContentService.MimeType.JSON);

      case "REBUILD_TODAY_CHARTS":
        return ContentService.createTextOutput(rebuildChartsForToday());

      case "REBUILD_ALL_CHARTS":
        return ContentService.createTextOutput(rebuildChartsForAllDailySheets());

      case "GET_RADAR_CONFIG":
        return JSONResponse(getRadarConfig(ss));

      case "SET_RADAR_CONFIG":
        return JSONResponse(setRadarConfig(ss, p));

      default:
        return ContentService.createTextOutput("Unbekannte Aktion. " + DEPLOY_MARKER);
    }
  }

  // Fall 2: Eine neue Messung wird gespeichert (vom Radar ausgelöst)
  if (speed && !isNaN(speed)) {
    const speedValue = parseInt(speed, 10);
    const time = new Date();
    const limit = parseInt(p.limit, 10) || 50;
    const isAlert = p.alert === "true" ? "🚨 ALARM (>20%)" : "Normal";
    const source = p.source || "RADAR";

    appendMeasurementRow(ss, time, speedValue, limit, isAlert, source);

    return ContentService.createTextOutput("Daten gespeichert.");
  }

  // Fall 3: Kein passender Parameter gefunden
  return ContentService.createTextOutput(DEPLOY_MARKER);
}

function getDailySheetName(date) {
  if (!date) date = new Date();
  const tz = getScriptTimeZone();
  const dayName = WEEKDAY_NAMES_DE[date.getDay()];
  const dateLabel = Utilities.formatDate(date, tz, "dd.MM.yyyy");
  return dayName + " " + dateLabel;
}

function getLegacyDailySheetName(date) {
  if (!(date instanceof Date)) date = new Date(date);
  const tz = getScriptTimeZone();
  return DAILY_SHEET_PREFIX + Utilities.formatDate(date, tz, "yyyy-MM-dd");
}

function getDailySheetNameCandidates(date) {
  return [getDailySheetName(date), getLegacyDailySheetName(date)];
}

function findDailyDataSheet(ss, date) {
  const candidates = getDailySheetNameCandidates(date);
  for (let i = 0; i < candidates.length; i++) {
    const sheet = ss.getSheetByName(candidates[i]);
    if (sheet) {
      return sheet;
    }
  }

  return null;
}

function getOrCreateDailyDataSheet(ss, date) {
  if (!ss) ss = SpreadsheetApp.openById(getSpreadsheetId());
  const sheetName = getDailySheetName(date);
  let sheet = findDailyDataSheet(ss, date);
  if (!sheet) {
    sheet = ss.insertSheet(sheetName);
    sheet.appendRow(DATA_HEADERS);
  } else if (sheet.getLastRow() === 0) {
    sheet.appendRow(DATA_HEADERS);
  }

  if (sheet.getName() !== sheetName) {
    // Nur umbenennen, wenn der Zielname noch nicht belegt ist.
    if (!ss.getSheetByName(sheetName)) {
      sheet.setName(sheetName);
    }
  }
  sheet.setFrozenRows(1);
  return sheet;
}

function handleLegacyEvent(ss, event, params) {
  const now = new Date();
  const limit = parseInt(params.limit || "50", 10) || 50;
  const source = params.source || "DISPLAY_EVENT";

  if (event.startsWith("radar_alarm_")) {
    const speedValue = parseInt(event.substring("radar_alarm_".length), 10) || 0;
    appendMeasurementRow(ss, now, speedValue, limit, "🚨 ALARM (>20%)", source);
    return "OK EVENT ALARM";
  }

  if (event.startsWith("email_user_")) {
    const payload = event.substring("email_user_".length);
    const firstSep = payload.indexOf('_');
    if (firstSep > 0) {
      const userId = payload.substring(0, firstSep);
      const email = payload.substring(firstSep + 1);
      upsertUserEmail(ss, userId, email);
      return "OK EVENT EMAIL";
    }
    return "ERROR EVENT EMAIL FORMAT";
  }

  // Konfig-/Steuer-Events (z.B. limit_50, alarm_toggle) nur bestaetigen.
  return "OK EVENT";
}

function upsertUserEmail(ss, userId, email) {
  if (!userId || !email) return;

  const userSheetName = getUserSheetName();
  let userSheet = ss.getSheetByName(userSheetName);
  if (!userSheet) {
    userSheet = ss.insertSheet(userSheetName);
    userSheet.appendRow(["id", "allowed", "role", "email", "name"]);
    userSheet.setFrozenRows(1);
  }

  const data = userSheet.getDataRange().getValues();
  for (let i = 1; i < data.length; i++) {
    if ((data[i][0] || "").toString() === userId.toString()) {
      userSheet.getRange(i + 1, 4).setValue(email);
      return;
    }
  }

  userSheet.appendRow([userId, false, "user", email, ""]);
}

function buildPieChartData(sheet) {
  const lastRow = sheet.getLastRow();
  if (lastRow < 2) return null;
  // Lese A:F ab Zeile 2 (ohne Header): [Zeit, Speed, Limit, Diff, Status, Quelle]
  const values = sheet.getRange(2, 1, lastRow - 1, 6).getValues();
  if (values.length === 0) return null;

  // Buckets: <=12, <=15, <=20, <=25, >25
  let c12 = 0, c15 = 0, c20 = 0, c25 = 0, cover = 0;
  let max = -1, maxTs = null;
  values.forEach(row => {
    const ts = row[0];
    const speed = Number(row[1]) || 0;
    const source = (row[5] || '').toString().toLowerCase();
    // Testeinträge vom Boot ("TESTBOOT") nicht berücksichtigen
    if (source.indexOf('testboot') >= 0) return;

    if (speed > max) { max = speed; maxTs = ts; }
    if (speed <= 12) c12++; else if (speed <= 15) c15++; else if (speed <= 20) c20++; else if (speed <= 25) c25++; else cover++;
  });

  // Schreibe aggregierte Daten (Label, Count) nach I:J
  const table = [["Kategorie", "Anzahl"],
                 ["≤ 12 km/h", c12],
                 ["13–15 km/h", c15],
                 ["16–20 km/h", c20],
                 ["21–25 km/h", c25],
                 ["> 25 km/h", cover]];

  const toClearRows = Math.max(sheet.getMaxRows(), table.length + 5);
  sheet.getRange(1, 9, toClearRows, 2).clearContent();
  sheet.getRange(1, 13, 2, 1).clearContent();
  sheet.getRange(1, 9, table.length, 2).setValues(table);

  return { rows: table.length, maxSpeed: max, maxTs: maxTs };
}

function ensureDailyCharts(sheet) {
  if (!sheet) return;
  const data = buildPieChartData(sheet);
  if (!data || data.rows <= 1) return;

  // Entferne bestehende Chart(s) an unserer Standardposition (I1)
  const charts = sheet.getCharts();
  charts.forEach(function(ch) {
    const info = ch.getContainerInfo();
    if (info && info.getAnchorRow() === 1 && info.getAnchorColumn() === 9) {
      sheet.removeChart(ch);
    }
  });

  // Torten-Diagramm aus I:J
  const range = sheet.getRange(1, 9, data.rows, 2); // I:J
  const chart = sheet.newChart()
    .asPieChart()
    .addRange(range)
    .setNumHeaders(1)
    .setOption('title', 'Geschwindigkeitsverteilung')
    .setPosition(1, 9, 0, 0) // I1
    .build();
  sheet.insertChart(chart);

  // Schnellsten separat anzeigen (Zelle M1)
  if (data.maxTs) {
    const tz = getScriptTimeZone();
    const ts = (data.maxTs instanceof Date) ? Utilities.formatDate(data.maxTs, tz, 'HH:mm') : String(data.maxTs);
    sheet.getRange('M1').setValue('Schnellster: ' + data.maxSpeed + ' km/h um ' + ts);
  }
}

function appendMeasurementRow(ss, timestamp, speedValue, limitValue, statusText, sourceText) {
  if (!ss) ss = SpreadsheetApp.openById(getSpreadsheetId());
  const diff = speedValue - limitValue;
  const sheet = getOrCreateDailyDataSheet(ss, timestamp);
  sheet.appendRow([
    timestamp,
    speedValue,
    limitValue,
    diff > 0 ? diff : 0,
    statusText || "Normal",
    sourceText || "RADAR"
  ]);

  // Charts initial anlegen oder periodisch (alle 10 Einträge) aktualisieren
  const rows = sheet.getLastRow();
  if (sheet.getCharts().length === 0 || ((rows - 1) % 10 === 0)) {
    ensureDailyCharts(sheet);
  }

  return { sheetName: sheet.getName() };
}

// --- Hilfsfunktionen für rückwirkende Diagramm-Erstellung ---
function isDailyDataHeaderRow_(values) {
  if (!values || values.length < 1) return false;
  const hdr = values[0] || [];
  if (hdr.length < DATA_HEADERS.length) return false;
  // prüfe die ersten N Spalten auf Match
  for (var i=0;i<DATA_HEADERS.length;i++) {
    if ((hdr[i] || '').toString().trim() !== DATA_HEADERS[i]) return false;
  }
  return true;
}

function rebuildChartsForToday() {
  const ss = SpreadsheetApp.openById(getSpreadsheetId());
  const sheet = getOrCreateDailyDataSheet(ss, new Date());
  ensureDailyCharts(sheet);
  return 'OK rebuild today: ' + sheet.getName();
}

function rebuildChartsForAllDailySheets() {
  const ss = SpreadsheetApp.openById(getSpreadsheetId());
  const sheets = ss.getSheets();
  var count = 0;
  for (var i=0;i<sheets.length;i++) {
    const s = sheets[i];
    const data = s.getDataRange().getValues();
    if (data && data.length>0 && isDailyDataHeaderRow_(data)) {
      try {
        ensureDailyCharts(s);
        count++;
      } catch (e) {
        // weiter mit dem nächsten Blatt
      }
    }
  }
  return 'OK rebuilt charts on ' + count + ' sheet(s).';
}

// --- HILFSFUNKTIONEN ---

function getUserEmail(ss, id) {
  const data = ss.getSheetByName(getUserSheetName()).getDataRange().getValues();
  for (let i = 1; i < data.length; i++) {
    if (data[i][0].toString() === id.toString()) {
      return data[i][3]; // Spalte D: Email
    }
  }
  return null;
}

function generateDailyStats(ss) {
  const sheet = findDailyDataSheet(ss, new Date());
  if (!sheet) return "Fehler: Tabelle 'Messungen' nicht gefunden.";

  const data = sheet.getDataRange().getValues();
  const today = new Date().toLocaleDateString("de-DE");
  let count = 0, over = 0, maxS = 0, alertCount = 0;

  // Start bei 1, um die Kopfzeile zu überspringen
  for (let i = 1; i < data.length; i++) {
    if (data[i][0] && data[i][0] instanceof Date) {
      const rowDate = data[i][0].toLocaleDateString("de-DE");
      if (rowDate === today) {
        count++;
        const speed = data[i][1];
        const diff = data[i][3];
        const alarm = data[i][4];

        if (diff > 0) over++;
        if (speed > maxS) maxS = speed;
        if (alarm && alarm.includes("ALARM")) alertCount++;
      }
    }
  }

  return "📊 Radar-Bericht (" + today + ")\n" +
         "--------------------------\n" +
         "🚗 Messungen: " + count + "\n" +
         "⚠️ Übertretungen: " + over + "\n" +
         "🚨 Kritisch: " + alertCount + "\n" +
         "🚀 Max Speed: " + maxS + " km/h";
}

function getDailyStatsObject(ss, targetDate) {
  const sheet = findDailyDataSheet(ss, targetDate);
  if (!sheet) {
    return { dateLabel: "-", count: 0, over: 0, maxS: 0, alertCount: 0 };
  }

  const data = sheet.getDataRange().getValues();
  const tz = getScriptTimeZone();
  const targetKey = Utilities.formatDate(targetDate, tz, "yyyy-MM-dd");
  const dateLabel = Utilities.formatDate(targetDate, tz, "dd.MM.yyyy");
  let count = 0;
  let over = 0;
  let maxS = 0;
  let alertCount = 0;

  for (let i = 1; i < data.length; i++) {
    if (data[i][0] && data[i][0] instanceof Date) {
      const rowKey = Utilities.formatDate(data[i][0], tz, "yyyy-MM-dd");
      if (rowKey === targetKey) {
        count++;
        const speed = Number(data[i][1]) || 0;
        const diff = Number(data[i][3]) || 0;
        const alarm = (data[i][4] || "").toString();
        if (diff > 0) over++;
        if (speed > maxS) maxS = speed;
        if (alarm.includes("ALARM")) alertCount++;
      }
    }
  }

  return { dateLabel, count, over, maxS, alertCount };
}

function publishAndGetDirectDownload(file) {
  try {
    file.setSharing(DriveApp.Access.ANYONE_WITH_LINK, DriveApp.Permission.VIEW);
  } catch (e) {
    // Ignoriere Fehler bei wiederholtem Setzen
  }
  return 'https://drive.google.com/uc?export=download&id=' + file.getId();
}

function ensureReportFolder() {
  // Versucht bevorzugt den konfigurierten Ordner zu öffnen.
  // Fallback: erstellt/benutzt einen Ordner "Radar-Reports" im eigenen Drive.
  try {
    if (typeof DRIVE_ROOT_FOLDER_ID !== 'undefined' && DRIVE_ROOT_FOLDER_ID) {
      return DriveApp.getFolderById(DRIVE_ROOT_FOLDER_ID);
    }
  } catch (e) {
    // fällt unten auf Namenssuche zurück
  }
  const name = 'Radar-Reports';
  const it = DriveApp.getFoldersByName(name);
  if (it.hasNext()) return it.next();
  return DriveApp.createFolder(name);
}

function createDailySummaryPdf(ss, targetDate, requesterId) {
  const stats = getDailyStatsObject(ss, targetDate);
  const tz = getScriptTimeZone();
  const fileDate = Utilities.formatDate(targetDate, tz, "yyyy-MM-dd");
  const nowLabel = Utilities.formatDate(new Date(), tz, "dd.MM.yyyy HH:mm");
  const folder = ensureReportFolder();

  const html =
    '<html><head><meta charset="UTF-8"></head><body style="font-family:Arial,sans-serif;">' +
    '<h1>Radar Tageszusammenfassung</h1>' +
    '<p><b>Datum:</b> ' + stats.dateLabel + '</p>' +
    '<p><b>Erstellt:</b> ' + nowLabel + '</p>' +
    '<hr />' +
    '<p>Messungen: <b>' + stats.count + '</b></p>' +
    '<p>Übertretungen: <b>' + stats.over + '</b></p>' +
    '<p>Kritische Alarme: <b>' + stats.alertCount + '</b></p>' +
    '<p>Max Speed: <b>' + stats.maxS + ' km/h</b></p>' +
    '<hr />' +
    '<p style="font-size:11px;color:#666;">Requester ID: ' + requesterId + '</p>' +
    '</body></html>';

  const blob = Utilities.newBlob(html, 'text/html', 'radar-report.html')
    .getAs(MimeType.PDF)
    .setName('Radar-Tagesbericht-' + fileDate + '.pdf');

  const file = folder.createFile(blob);
  const directUrl = publishAndGetDirectDownload(file);
  return { id: file.getId(), url: directUrl, name: file.getName() };
}

function getLastTwoHoursStatsObject(ss, now) {
  const tz = getScriptTimeZone();
  const from = new Date(now.getTime() - 2 * 60 * 60 * 1000);
  const sheetNames = {};
  getDailySheetNameCandidates(from).forEach(name => sheetNames[name] = true);
  getDailySheetNameCandidates(now).forEach(name => sheetNames[name] = true);

  const rows = [];
  Object.keys(sheetNames).forEach(name => {
    const s = ss.getSheetByName(name);
    if (!s) return;
    const values = s.getDataRange().getValues();
    for (let i = 1; i < values.length; i++) {
      rows.push(values[i]);
    }
  });

  let count = 0;
  let over = 0;
  let maxS = 0;
  let alertCount = 0;

  for (let i = 0; i < rows.length; i++) {
    const t = rows[i][0];
    if (t && t instanceof Date && t >= from && t <= now) {
      count++;
      const speed = Number(rows[i][1]) || 0;
      const diff = Number(rows[i][3]) || 0;
      const alarm = (rows[i][4] || "").toString();
      if (diff > 0) over++;
      if (speed > maxS) maxS = speed;
      if (alarm.includes("ALARM")) alertCount++;
    }
  }

  return {
    count,
    over,
    maxS,
    alertCount,
    startLabel: Utilities.formatDate(from, tz, "dd.MM.yyyy HH:mm"),
    endLabel: Utilities.formatDate(now, tz, "dd.MM.yyyy HH:mm")
  };
}

function cleanupOldTwoHourPdfs() {
  try {
    const folder = ensureReportFolder();
    const files = folder.getFiles();
    const now = new Date();
    const maxAgeMs = 2 * 24 * 60 * 60 * 1000;

    while (files.hasNext()) {
      const file = files.next();
      const name = file.getName();
      if (!name.startsWith(TWO_HOUR_PDF_PREFIX)) {
        continue;
      }
      const ageMs = now.getTime() - file.getDateCreated().getTime();
      if (ageMs > maxAgeMs) {
        file.setTrashed(true);
      }
    }
  } catch (e) {
    // Falls kein Drive-Zugriff/Ordner: Cleanup überspringen, PDF-Erstellung geht weiter
    console.warn('cleanupOldTwoHourPdfs skipped: ' + e);
  }
}

function createTwoHourSummaryPdf(ss, now, requesterId) {
  // Fallback, falls now nicht gesetzt ist
  if (!now) now = new Date();
  cleanupOldTwoHourPdfs();

  const stats = getLastTwoHoursStatsObject(ss, now);
  const tz = getScriptTimeZone();
  const fileTs = Utilities.formatDate(now, tz, "yyyy-MM-dd-HHmm");
  const nowLabel = Utilities.formatDate(now, tz, "dd.MM.yyyy HH:mm");
  const folder = ensureReportFolder();

  const html =
    '<html><head><meta charset="UTF-8"></head><body style="font-family:Arial,sans-serif;">' +
    '<h1>Radar 2h-Zusammenfassung</h1>' +
    '<p><b>Zeitraum:</b> ' + stats.startLabel + ' bis ' + stats.endLabel + '</p>' +
    '<p><b>Erstellt:</b> ' + nowLabel + '</p>' +
    '<hr />' +
    '<p>Messungen: <b>' + stats.count + '</b></p>' +
    '<p>Übertretungen: <b>' + stats.over + '</b></p>' +
    '<p>Kritische Alarme: <b>' + stats.alertCount + '</b></p>' +
    '<p>Max Speed: <b>' + stats.maxS + ' km/h</b></p>' +
    '<hr />' +
    '<p style="font-size:11px;color:#666;">Requester ID: ' + requesterId + '</p>' +
    '<p style="font-size:11px;color:#666;">Hinweis: 2h-PDFs werden nach 2 Tagen automatisch geloescht.</p>' +
    '</body></html>';

  const blob = Utilities.newBlob(html, 'text/html', 'radar-2h-report.html')
    .getAs(MimeType.PDF)
    .setName(TWO_HOUR_PDF_PREFIX + fileTs + '.pdf');

  const file = folder.createFile(blob);
  const directUrl = publishAndGetDirectDownload(file);
  return { id: file.getId(), url: directUrl, name: file.getName() };
}

function sendReport(email, ss) {
  const stats = generateDailyStats(ss);
  MailApp.sendEmail({
    to: email,
    subject: "Dein Radar-System Report",
    body: "Guten Tag,\n\nhier ist deine tägliche Zusammenfassung:\n\n" + stats + "\n\nGesendet von deinem ESP32-Radar."
  });
}

function JSONResponse(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function getDefaultRadarConfig() {
  return {
    angle_min: -45,
    angle_max: 45,
    min_speed_kmh: 1,
    sample_window_ms: 3000,
    sensor_range_min: 2,
    sensor_range_max: 20,
    sensor_sensitivity: 4,
    mount_angle_deg: 12,
    speed_limit_kmh: 50
  };
}

function getOrCreateRadarConfigSheet(ss) {
  let sheet = ss.getSheetByName(RADAR_CONFIG_SHEET_NAME);
  if (!sheet) {
    sheet = ss.insertSheet(RADAR_CONFIG_SHEET_NAME);
  }

  if (sheet.getLastRow() === 0) {
    sheet.appendRow(["key", "value"]);
    const defaults = getDefaultRadarConfig();
    Object.keys(defaults).forEach(function(key) {
      sheet.appendRow([key, defaults[key]]);
    });
  }

  sheet.setFrozenRows(1);
  return sheet;
}

function getRadarConfig(ss) {
  const sheet = getOrCreateRadarConfigSheet(ss);
  const values = sheet.getDataRange().getValues();
  const cfg = getDefaultRadarConfig();

  for (let i = 1; i < values.length; i++) {
    const key = values[i][0];
    const value = values[i][1];
    if (key !== "" && key != null) {
      cfg[String(key)] = (value === "" || value == null) ? cfg[String(key)] : Number(value);
    }
  }

  return cfg;
}

function setRadarConfig(ss, params) {
  const current = getRadarConfig(ss);
  const sheet = getOrCreateRadarConfigSheet(ss);
  const next = Object.assign({}, current, {
    angle_min: Number(params.angle_min != null ? params.angle_min : current.angle_min),
    angle_max: Number(params.angle_max != null ? params.angle_max : current.angle_max),
    min_speed_kmh: Number(params.min_speed_kmh != null ? params.min_speed_kmh : current.min_speed_kmh),
    sample_window_ms: Number(params.sample_window_ms != null ? params.sample_window_ms : current.sample_window_ms),
    sensor_range_min: Number(params.sensor_range_min != null ? params.sensor_range_min : current.sensor_range_min),
    sensor_range_max: Number(params.sensor_range_max != null ? params.sensor_range_max : current.sensor_range_max),
    sensor_sensitivity: Number(params.sensor_sensitivity != null ? params.sensor_sensitivity : current.sensor_sensitivity),
    mount_angle_deg: Number(params.mount_angle_deg != null ? params.mount_angle_deg : current.mount_angle_deg),
    speed_limit_kmh: Number(params.speed_limit_kmh != null ? params.speed_limit_kmh : current.speed_limit_kmh)
  });

  const rows = [["key", "value"]];
  Object.keys(next).forEach(function(key) {
    rows.push([key, next[key]]);
  });

  sheet.clearContents();
  sheet.getRange(1, 1, rows.length, 2).setValues(rows);
  sheet.setFrozenRows(1);

  return { ok: true, config: next };
}

// --- Liefert den letzten Messwert als JSON für das Display ---
function getLastMeasurementAsJson(ss) {
  const sheet = findDailyDataSheet(ss, new Date());
  if (!sheet) {
    return JSON.stringify({speed:0, limit:0, time:"--:--", ip:"-", dot:0});
  }
  const data = sheet.getDataRange().getValues();
  if (data.length < 2) {
    return JSON.stringify({speed:0, limit:0, time:"--:--", ip:"-", dot:0});
  }
  const last = data[data.length - 1];
  // Annahme: Spalten = [Zeitstempel, Geschwindigkeit, Limit, Überschreitung, Status, Quelle]
  return JSON.stringify({
    speed: last[1] || 0,
    limit: last[2] || 0,
    time: Utilities.formatDate(new Date(last[0]), getScriptTimeZone(), "HH:mm"),
    ip: "-", // Optional: IP kann aus Quelle extrahiert werden, falls gespeichert
    dot: (last[3] && last[3] > 0) ? 1 : 0
  });
}
