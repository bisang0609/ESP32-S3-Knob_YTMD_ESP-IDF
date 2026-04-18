/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/* Modified for project: RGB565, 32-bit MCU     */
/*----------------------------------------------*/

#define JD_SZBUF        512

#define JD_FORMAT       1
/* 0: RGB888, 1: RGB565 (LE), 2: Grayscale */

#define JD_USE_SCALE    1
/* 0: Disable, 1: Enable 1/2/4/8 output scaling */

#define JD_TBLCLIP      1
/* 0: Disable, 1: Enable (faster, +1KB code) */

#define JD_FASTDECODE   1
/* 0: Basic (8/16-bit MCU), 1: +barrel shifter (32-bit MCU) */
