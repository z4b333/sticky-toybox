/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*                                              */
/* Toybox configuration: grayscale output       */
/* (covers become 1-bit thumbnails, so colour   */
/* would be thrown away anyway), descaling on   */
/* (large covers decode at 1/2..1/8), basic     */
/* 32-bit fast decode (~3.5 KB workspace).      */
/*                                              */
/* The ESP32-S3 mask ROM carries a 2012 build   */
/* of this module behind weak PROVIDE symbols;  */
/* vendoring our own (strong) definitions       */
/* overrides it cleanly, and buys grayscale     */
/* output the ROM build does not have.          */
/*----------------------------------------------*/

#define JD_SZBUF        512
#define JD_FORMAT       2   /* grayscale, 1 byte/pixel */
#define JD_USE_SCALE    1
#define JD_TBLCLIP      1
#define JD_FASTDECODE   1
