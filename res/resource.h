#pragma once

// Icons
#define IDI_APP 1

// Strings (res/strings.rc)
#define IDS_APP_NAME 100
#define IDS_LABEL_UP 101    // overlay letters mode, upload line
#define IDS_LABEL_DOWN 102  // overlay letters mode, download line
#define IDS_NO_VALUE 103    // overlay placeholder when there is no speed yet

// Overlay tooltip (app shell)
#define IDS_TIP_AUTO 110       // "Automatic: %s", %s = the internet adapter's display name
#define IDS_TIP_AUTO_NONE 111  // Automatic mode with no internet adapter

// Right-click menu
#define IDS_MENU_ADAPTER 120
#define IDS_MENU_AUTOMATIC 121
#define IDS_MENU_UNITS 122
#define IDS_MENU_BITS 123
#define IDS_MENU_BYTES 124
#define IDS_MENU_SHOW_GRAPH 125
#define IDS_MENU_LETTERS 126
#define IDS_MENU_OPEN_REPORT 127
#define IDS_MENU_SETTINGS 128
#define IDS_MENU_EXIT 129

// Report window (Task 15)
#define IDS_REPORT_TITLE 200
#define IDS_PERIOD_LABEL 201
#define IDS_PERIOD_TODAY 202
#define IDS_PERIOD_YESTERDAY 203
#define IDS_PERIOD_THIS_WEEK 204
#define IDS_PERIOD_LAST_WEEK 205
#define IDS_PERIOD_THIS_MONTH 206
#define IDS_PERIOD_LAST_MONTH 207
#define IDS_PERIOD_LAST_30_DAYS 208
#define IDS_PERIOD_THIS_YEAR 209
#define IDS_PERIOD_ALL_TIME 210
#define IDS_PERIOD_CUSTOM 211
#define IDS_RANGE_FORMAT 212  // "%s – %s": first and last day of a period
#define IDS_BTN_EXPORT 213
#define IDS_BTN_CLEAR_ALL 214
#define IDS_TAB_SUMMARY 215
#define IDS_TAB_TIMELINE 216
#define IDS_TAB_ADAPTERS 217
#define IDS_TAB_WIFI 218

// Summary tab
#define IDS_RX_TX 220  // "↓ %s · ↑ %s": received, sent
#define IDS_BUSIEST_DAY 221
#define IDS_TOP_ADAPTERS 222
#define IDS_TOP_WIFI 223
#define IDS_NO_USAGE 224
#define IDS_NOTE_COVERAGE 225
#define IDS_NOTE_CORRUPT 226    // %s = where the unreadable database was moved
#define IDS_NOTE_UNHEALTHY 227  // the store reported a write or read failure

// Timeline tab
#define IDS_LEGEND_RECEIVED 230
#define IDS_LEGEND_SENT 231
#define IDS_BAR_HOUR 232  // "%s, %s – %s": date, hour start, hour end
#define IDS_TIP_RECEIVED 233
#define IDS_TIP_SENT 234
#define IDS_TIP_TOTAL 235

// Adapters and Wi-Fi networks tabs
#define IDS_COL_NAME 240
#define IDS_COL_TYPE 241
#define IDS_COL_RECEIVED 242
#define IDS_COL_SENT 243
#define IDS_COL_TOTAL 244
#define IDS_KIND_WIFI 245
#define IDS_KIND_ETHERNET 246
#define IDS_KIND_VIRTUAL 247
#define IDS_KIND_OTHER 248
#define IDS_SHOW_HIDDEN 249
#define IDS_ADAPTER_HIDDEN 250  // "%s (hidden)"
#define IDS_UNKNOWN_WIFI 251
#define IDS_NOTE_RAW 252
#define IDS_MENU_RENAME 253
#define IDS_MENU_HIDE 254
#define IDS_MENU_UNHIDE 255
#define IDS_MENU_CLEAR_ADAPTER 256
#define IDS_MENU_CLEAR_NETWORK 257

// Actions and dialogs
#define IDS_CONFIRM_CLEAR_ALL 260
#define IDS_CONFIRM_CLEAR_ONE 261  // %s = the adapter or network name
#define IDS_BTN_DELETE 262
#define IDS_BTN_CANCEL 263
#define IDS_BTN_OK 264
#define IDS_EXPORT_TITLE 265
#define IDS_CSV_FILTER 266
#define IDS_EXPORT_FAILED 267
#define IDS_RENAME_TITLE 268
#define IDS_RENAME_PROMPT 269  // %s = the Windows name
#define IDS_RANGE_TITLE 270
#define IDS_RANGE_FROM 271
#define IDS_RANGE_TO 272

// Dialog templates (res/app.rc) and their controls
#define IDD_RENAME 300
#define IDD_RANGE 301
#define IDC_RENAME_PROMPT 1001
#define IDC_RENAME_EDIT 1002
#define IDC_RANGE_FROM_LABEL 1003
#define IDC_RANGE_FROM 1004
#define IDC_RANGE_TO_LABEL 1005
#define IDC_RANGE_TO 1006
