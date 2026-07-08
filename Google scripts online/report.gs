// In report.gs

function generateDailyStatsReport(ss) {
  if (!ss) {
    if (typeof getSpreadsheetId === "function") {
      ss = SpreadsheetApp.openById(getSpreadsheetId());
    } else if (typeof SPREADSHEET_ID !== "undefined" && SPREADSHEET_ID) {
      ss = SpreadsheetApp.openById(SPREADSHEET_ID);
    } else {
      ss = SpreadsheetApp.getActive();
    }
  }

  const measureSheetName = (typeof MEASURE_SHEET !== "undefined" && MEASURE_SHEET)
    ? MEASURE_SHEET
    : ((typeof getMeasureSheetName === "function") ? getMeasureSheetName() : "Messungen");

  const sheet = ss.getSheetByName(measureSheetName);
  if (!sheet) {
    return "📊 *Statistik von heute*\n\n🚗 Fahrzeuge gesamt: 0\n⚠️ Übertretungen: 0\n🚀 Höchstwert: 0 km/h";
  }

  const data = sheet.getDataRange().getValues();
  const tz = (typeof TZ !== "undefined" && TZ) ? TZ : "Europe/Berlin";
  const today = Utilities.formatDate(new Date(), tz, "dd.MM.yyyy");

  let count = 0;
  let over = 0;
  let maxV = 0;

  // Wir fangen bei Zeile 2 an (Index 1)
  for (let i = 1; i < data.length; i++) {
    const rowTime = data[i][0];
    if (!(rowTime instanceof Date)) {
      continue;
    }

    const rowDate = Utilities.formatDate(rowTime, tz, "dd.MM.yyyy");
    if (rowDate === today) {
      count++;
      const speed = parseInt(data[i][1], 10) || 0;
      const diff = Number(data[i][3]) || 0; // Spalte D: Überschreitung
      if (diff > 0) over++;
      if (speed > maxV) maxV = speed;
    }
  }

  return "📊 *Statistik von heute* (" + today + ")\n\n" +
         "🚗 Fahrzeuge gesamt: " + count + "\n" +
         "⚠️ Übertretungen: " + over + "\n" +
         "🚀 Höchstwert: " + maxV + " km/h";
}

function reportHandleGet(e) {
  try {
    const ss = SpreadsheetApp.openById((typeof getSpreadsheetId === "function") ? getSpreadsheetId() : SPREADSHEET_ID);
    const measureSheetName = (typeof MEASURE_SHEET !== "undefined" && MEASURE_SHEET)
      ? MEASURE_SHEET
      : ((typeof getMeasureSheetName === "function") ? getMeasureSheetName() : "Messungen");

    const sheet = ss.getSheetByName(measureSheetName) || ss.insertSheet(measureSheetName);

    const p = (e && e.parameter) ? e.parameter : {};
    const speed = parseFloat(p.speed) || 0;
    const limit = parseInt(p.limit, 10) || 0;
    const over = speed > limit ? (speed - limit) : 0;

    // Zeile schreiben: Datum/Zeit | Geschwindigkeit | Limit | Übertretung | Status | Quelle
    const status = over > 0 ? "🚨 Übertretung" : "OK";
    sheet.appendRow([
      new Date(),
      speed,
      limit,
      over,
      status,
      "ESP"
    ]);

    // Rückmeldung an den ESP32 (wichtig für HTTP Code 200)
    return ContentService.createTextOutput("OK");

  } catch (err) {
    return ContentService.createTextOutput("Fehler: " + err.toString());
  }
}
