function doPost(e) {
  if (!e || !e.postData || !e.postData.contents) {
    return ContentService.createTextOutput("OK");
  }

  const u = JSON.parse(e.postData.contents);

  if (u.callback_query) {
    if (typeof handleCallback === "function") {
      handleCallback(u.callback_query);
    }
    return ContentService.createTextOutput("OK");
  }

  if (u.message) {
    if (typeof handleTelegram === "function") {
      handleTelegram(
        u.message.text || "",
        u.message.chat.id,
        u.message.from.first_name || ""
      );
    }
    return ContentService.createTextOutput("OK");
  }

  return ContentService.createTextOutput("OK");
}

function telegramApi(method, payload) {
  return UrlFetchApp.fetch(
    "https://api.telegram.org/bot" + BOT_TOKEN + "/" + method,
    {
      method: "post",
      contentType: "application/json",
      payload: JSON.stringify(payload),
      muteHttpExceptions: true
    }
  );
}

function sendTelegramMessage(chatId, text) {
  return telegramApi("sendMessage", {
    chat_id: chatId,
    text: text,
    parse_mode: "Markdown"
  });
}

// Einmalig ausfuehren, um den Bot mit Google zu verbinden
function setWebhook() {
  const url = ScriptApp.getService().getUrl();
  const tgUrl = "https://api.telegram.org/bot" + BOT_TOKEN + "/setWebhook?url=" + encodeURIComponent(url);
  const response = UrlFetchApp.fetch(tgUrl, { muteHttpExceptions: true });
  console.log(response.getContentText());
}
