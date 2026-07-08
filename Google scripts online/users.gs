function getUser(ss, id) {
  if (!ss || id === undefined || id === null || id === "") {
    return null;
  }

  const sheet = ss.getSheetByName(USER_SHEET);
  if (!sheet) return null;

  const data = sheet.getDataRange().getValues();
  const searchId = String(id).trim();

  for (let i = 1; i < data.length; i++) {
    const sheetId = String(data[i][0] || "").trim();
    if (sheetId === searchId) {
      const isAllowed = data[i][1] === true || String(data[i][1]).toLowerCase() === "true";
      return {
        id: sheetId,
        allowed: isAllowed,
        role: data[i][2] || "user",
        email: data[i][3] || "",
        name: data[i][4] || ""
      };
    }
  }
  return null;
}

function registerUser(ss, chatId, name) {
  if (!ss) {
    return { created: false, exists: false, id: "" };
  }

  let sheet = ss.getSheetByName(USER_SHEET);
  if (!sheet) {
    sheet = ss.insertSheet(USER_SHEET);
    sheet.appendRow(["id", "allowed", "role", "email", "name"]);
    sheet.setFrozenRows(1);
  }

  const userId = String(chatId || "").trim();
  const displayName = String(name || "User").trim();
  if (!userId) {
    return { created: false, exists: false, id: "" };
  }

  const existing = getUser(ss, userId);
  if (existing) {
    return { created: false, exists: true, id: userId };
  }

  sheet.appendRow([userId, false, "user", "", displayName]);
  return { created: true, exists: false, id: userId };
}
