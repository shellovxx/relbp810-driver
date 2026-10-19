// Resource IDs of CNLB810U.DLL. Dialog, control, icon, bitmap and string numbers are the ones of
// the original Canon CAP1UIK.DLL so that the converted templates (dialogs.rc) work unchanged.
#pragma once

// Dialogs
#define IDD_PAGESETUP        100
#define IDD_FINISHING        101
#define IDD_QUALITY          102
#define IDD_OVERLAY          104
#define IDD_FINISH_SETTINGS  105
#define IDD_ABOUT            106
#define IDD_ADVANCED         107
#define IDD_VIEW_SETTINGS    108
#define IDD_PROFILE_ADD      109
#define IDD_PROFILE_EDIT     110

// Controls shared by the three pages
#define IDC_PROFILE          1002
#define IDC_PROFILE_ADD      1003
#define IDC_PROFILE_EDIT     1004
#define IDC_PREVIEW          1007
#define IDC_PREVIEW_ICON     1008
#define IDC_PREVIEW_TEXT     1009
#define IDC_VIEW_SETTINGS    1010

// Page setup
#define IDC_PAPER_SIZE       1202
#define IDC_OUTPUT_SIZE      1205
#define IDC_COPIES           1207
#define IDC_COPIES_SPIN      1208
#define IDC_PORTRAIT         1212
#define IDC_LANDSCAPE        1214
#define IDC_LAYOUT           1217
#define IDC_ORDER_LABEL      1218
#define IDC_ORDER            1219
#define IDC_MANUAL_SCALE     1220
#define IDC_SCALE            1221
#define IDC_SCALE_SPIN       1222
#define IDC_SCALE_PERCENT    1223
#define IDC_OVERLAY          1226
#define IDC_PAGESETUP_RESET  1227
#define IDC_OUTPUT_ICON      1203
#define IDC_OUTPUT_LABEL     1204

// Finishing
#define IDC_COLLATE_ICON     1350
#define IDC_COLLATE          1351
#define IDC_FINISH_SETTINGS  1353
#define IDC_FINISHING_RESET  1354

// Quality
#define IDC_HALFTONE_IMAGE   1450
#define IDC_RES600           1452
#define IDC_RES300           1453
#define IDC_HT_PATTERN1      1456
#define IDC_HT_PATTERN2      1457
#define IDC_HT_SMOOTH        1458
#define IDC_BRIGHTNESS       1462
#define IDC_BRIGHTNESS_EDIT  1464
#define IDC_BRIGHTNESS_SPIN  1465
#define IDC_CONTRAST         1469
#define IDC_CONTRAST_EDIT    1471
#define IDC_CONTRAST_SPIN    1472
#define IDC_ABOUT            1475
#define IDC_ADVANCED         1476
#define IDC_QUALITY_RESET    1477

// Overlay dialog
#define IDC_OVL_FILE         1301
#define IDC_OVL_BROWSE       1302
#define IDC_OVL_PREVIEW      1304
#define IDC_OVL_SAMPLE       1305
#define IDC_OVL_PLACE_ICON   1308
#define IDC_OVL_BACK         1309
#define IDC_OVL_FRONT        1310
#define IDC_OVL_LEFT         1314
#define IDC_OVL_LEFT_SPIN    1315
#define IDC_OVL_TOP          1317
#define IDC_OVL_TOP_SPIN     1318
#define IDC_OVL_WIDTH        1321
#define IDC_OVL_WIDTH_SPIN   1322
#define IDC_OVL_HEIGHT       1324
#define IDC_OVL_HEIGHT_SPIN  1325
#define IDC_OVL_CM           1328
#define IDC_OVL_INCH         1329
#define IDC_OVL_OK           1331
#define IDC_OVL_CANCEL       1332
#define IDC_OVL_RESET        1333
#define IDC_OVL_HELP         1334

// Finishing "Settings" dialog
#define IDC_FS_MISMATCH      1400
#define IDC_FS_EMF           1401
#define IDC_FS_OK            1403
#define IDC_FS_CANCEL        1404
#define IDC_FS_RESET         1405
#define IDC_FS_HELP          1406

// About
#define IDC_ABOUT_LINE1      1500
#define IDC_ABOUT_DRIVER     1501
#define IDC_ABOUT_VERSION    1503
#define IDC_ABOUT_MSCOPY     1506
#define IDC_ABOUT_COPYRIGHT  1504
#define IDC_ABOUT_RIGHTS     1505
#define IDC_ABOUT_OK         1507

// Quality "Advanced" dialog
#define IDC_ADV_DENSITY      1553
#define IDC_ADV_TONERSAVE    1556
#define IDC_ADV_REFINE       1558
#define IDC_ADV_OK           1560
#define IDC_ADV_CANCEL       1561
#define IDC_ADV_RESET        1562
#define IDC_ADV_HELP         1563

// View settings
#define IDC_VS_TAB           1050
#define IDC_VS_OK            1053

// Add profile
#define IDC_PA_NAME          1101
#define IDC_PA_ICON          1103
#define IDC_PA_NOTES         1105
#define IDC_PA_DETAILS       1106
#define IDC_PA_SEP           1107
#define IDC_PA_TAB           1108
#define IDC_PA_SEP2          1109
#define IDC_PA_OK            1110
#define IDC_PA_CANCEL        1111
#define IDC_PA_HELP          1112

// Edit profiles
#define IDC_PE_LIST          1151
#define IDC_PE_DELETE        1153
#define IDC_PE_NAME          1157
#define IDC_PE_ICON          1159
#define IDC_PE_NOTES         1161
#define IDC_PE_IMPORT        1162
#define IDC_PE_EXPORT        1163
#define IDC_PE_TAB           1165
#define IDC_PE_OK            1167
#define IDC_PE_CANCEL        1168
#define IDC_PE_HELP          1169
#define IDC_PE_UP            1170
#define IDC_PE_DOWN          1171

// Icons (CAP1UIK numbers)
#define IDI_PAPER_SIZE       102
#define IDI_OUTPUT_SIZE      103
#define IDI_PORTRAIT         104
#define IDI_LANDSCAPE        105
#define IDI_OVL_FRONT        106
#define IDI_OVL_BACK         107
#define IDI_COLLATE          110
#define IDI_NOCOLLATE        111
#define IDI_ORDER_FIRST      118   // 118..125: page order arrows
#define IDI_PROFILE_FIRST    126   // 126..168: profile icons

// Bitmaps
#define IDB_COLLATE          200
#define IDB_TEXTLINE         206
#define IDB_HALFTONE_SAMPLE  208
#define IDB_PAGE             213
#define IDB_RES300           210
#define IDB_RES600           211
#define IDB_RES_MASK         212

// Strings of our own (Canon's are 78..7311)
#define IDS_DRIVER_NAME      9000
#define IDS_PROFILE_NONE     9001
