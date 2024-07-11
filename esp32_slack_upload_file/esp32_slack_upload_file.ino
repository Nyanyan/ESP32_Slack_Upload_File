/*
  Upload File to Slack with ESP32

  @date 2024
  @author Takuto Yamana
  @license MIT license
  @note please see article here: https://qiita.com/Nyanyan_Cube/items/c6e74ce6e6c8cd159df7
*/

#include <WiFi.h>         // to use wifi
#include <HTTPClient.h>   // to use http POST
#include <ArduinoJson.h>  // to parse json

// slack token & channel id
#define SLACK_TOKEN "slack token"
#define SLACK_CHANNEL_ID "channel id"

// Wifi SSID & password
#define WIFI_SSID "wifi ssid"
#define WIFI_PASS "wifi password"

// slack url
#define SLACK_URL_GET_UPLOAD_URL "https://slack.com/api/files.getUploadURLExternal"
#define SLACK_URL_COMPLETE_UPLOAD "https://slack.com/api/files.completeUploadExternal"

// bitmap information
#define BMP_BIT_PER_PIXEL 24                                                // 24 bytes for each pixel (8 bytes * 3 colors)
#define BMP_HEADER_BYTE (14 + 40)                                           // 14 bytes for file header + 40 bytes for information header
#define BMP_N_COLOR_PALETTE 0                                               // color palette is not used
#define BMP_OFFSET_TO_IMG_DATA (BMP_HEADER_BYTE + BMP_N_COLOR_PALETTE * 4)  // offset to main image data

// image information
#define IMG_WIDTH 20
#define IMG_HEIGHT 20
#define IMG_FILE_NAME "img.bmp"
#define IMG_FILE_TYPE "image/bmp"
#define IMG_FILE_SIZE (BMP_OFFSET_TO_IMG_DATA + IMG_HEIGHT * IMG_WIDTH * BMP_BIT_PER_PIXEL / 8)  // img.bmp file size

// http multipart
#define HTTP_BOUNDARY "boundary"

// json
StaticJsonDocument<512> doc;

void setup() {
  /********** initialize **********/
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  /********** initialize **********/


  /********** create image **********/
  uint8_t img_body[IMG_HEIGHT][IMG_WIDTH][3];  // image array note: the order is RGB

  // set white to all pixel
  for (int y = 0; y < IMG_HEIGHT; ++y) {
    for (int x = 0; x < IMG_WIDTH; ++x) {
      for (int i = 0; i < 3; ++i) {
        img_body[y][x][i] = 255;
      }
    }
  }

  // draw diagonal red line
  for (int x = 0; x < IMG_WIDTH; ++x) {
    int y = IMG_HEIGHT * x / IMG_WIDTH;
    img_body[y][x][0] = 255;  // Red
    img_body[y][x][1] = 0;    // Green
    img_body[y][x][2] = 0;    // Blue
  }

  // draw blue frame
  for (int x = 0; x < IMG_WIDTH; ++x) {
    img_body[0][x][0] = 0;                 // Red
    img_body[0][x][1] = 0;                 // Green
    img_body[0][x][2] = 255;               // Blue
    img_body[IMG_HEIGHT - 1][x][0] = 0;    // Red
    img_body[IMG_HEIGHT - 1][x][1] = 0;    // Green
    img_body[IMG_HEIGHT - 1][x][2] = 255;  // Blue
  }
  for (int y = 0; y < IMG_HEIGHT; ++y) {
    img_body[y][0][0] = 0;                // Red
    img_body[y][0][1] = 0;                // Green
    img_body[y][0][2] = 255;              // Blue
    img_body[y][IMG_WIDTH - 1][0] = 0;    // Red
    img_body[y][IMG_WIDTH - 1][1] = 0;    // Green
    img_body[y][IMG_WIDTH - 1][2] = 255;  // Blue
  }
  /********** create image **********/


  /********** create bitmap image **********/
  uint8_t bmp_img[IMG_FILE_SIZE];  // bmp image
  uint16_t* bmp_img_16;            // bmp header as uint16_t
  uint32_t* bmp_img_32;            // bmp header as uint32_t

  // file header
  bmp_img[0] = 'B';  // bfType: always B
  bmp_img[1] = 'M';  // bfType: always M
  bmp_img_32 = (uint32_t*)(bmp_img + 2);
  bmp_img_32[0] = IMG_FILE_SIZE;           // bfSize: file size
  bmp_img_32[1] = 0;                       // bfReserved1 & bfReserved2: always 0
  bmp_img_32[2] = BMP_OFFSET_TO_IMG_DATA;  // bfOffBits: offset to main image data

  // information header
  bmp_img_32[3] = 40;          // bcSize: 40 bytes
  bmp_img_32[4] = IMG_WIDTH;   // bcWidth: image width
  bmp_img_32[5] = IMG_HEIGHT;  // bcHeight: image height
  bmp_img_16 = (uint16_t*)(bmp_img + 26);
  bmp_img_16[0] = 1;                  // bcPlanes: always 1
  bmp_img_16[1] = BMP_BIT_PER_PIXEL;  // bcBitCount: bit per pixel
  bmp_img_32 = (uint32_t*)(bmp_img + 30);
  bmp_img_32[0] = 0;                    // biCompression: compression, 0 = no compression
  bmp_img_32[1] = 3780;                 // biSizeImage: 3780 = 96 dpi
  bmp_img_32[2] = 3780;                 // biXPixPerMeter: 3780 = 96 dpi
  bmp_img_32[3] = 3780;                 // biYPixPerMeter: 3780 = 96 dpi
  bmp_img_32[4] = BMP_N_COLOR_PALETTE;  // biClrUsed: color palette size
  bmp_img_32[5] = BMP_N_COLOR_PALETTE;  // biCirImportant: important color palette size

  // color palette (not used)
  /*
  for (int i = 0; i < BMP_N_COLOR_PALETTE; ++i){
    bmp_img[BMP_HEADER_BYTE + 4 * i + 0] = 0; // Blue
    bmp_img[BMP_HEADER_BYTE + 4 * i + 1] = 0; // Green
    bmp_img[BMP_HEADER_BYTE + 4 * i + 2] = 255; // Red
    bmp_img[BMP_HEADER_BYTE + 4 * i + 3] = 0; // always 0
  }
  */

  // main image data
  int bmp_idx = BMP_OFFSET_TO_IMG_DATA;
  int n_pixel_per_elem = 0;
  for (int y = IMG_HEIGHT - 1; y >= 0; --y) {  // bottom is 0
    for (int x = 0; x < IMG_WIDTH; ++x) {
      for (int i = 2; i >= 0; --i) {  // RGB -> BGR
        bmp_img[bmp_idx++] = img_body[y][x][i];
      }
    }
  }
  /********** create bitmap image **********/


  /********** upload image **********/
  HTTPClient http;
  String body, received_string;
  int status_code;

  // files.getUploadURLExternal
  if (!http.begin(SLACK_URL_GET_UPLOAD_URL)) {
    Serial.println(String("[ERROR] cannot begin ") + SLACK_URL_GET_UPLOAD_URL);
    return;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  body = String("token=") + SLACK_TOKEN + "&length=" + String(IMG_FILE_SIZE) + "&filename=" + IMG_FILE_NAME;
  status_code = http.POST(body);
  if (status_code != HTTP_CODE_OK && status_code != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.println("[ERROR] status code error: " + String(status_code));
    return;
  }
  received_string = http.getString();
  deserializeJson(doc, received_string);
  if (doc["ok"] != true) {
    Serial.print("[ERROR] slack response is not ok");
    return;
  }
  const char* upload_url = doc["upload_url"];
  const char* file_id = doc["file_id"];
  Serial.print(String("upload_url: ") + upload_url);
  Serial.print(String("file_id: ") + file_id);
  http.end();

  // upload image to upload_url
  if (!http.begin(upload_url)) {
    Serial.println(String("[ERROR] cannot begin ") + upload_url);
    return;
  }
  http.addHeader("Content-Type", String("multipart/form-data; boundary=") + HTTP_BOUNDARY);
  // create multipart header
  String multipart_header = "";
  multipart_header += String("--") + HTTP_BOUNDARY + "\r\n";
  multipart_header += String("Content-Disposition: form-data; name=\"uploadFile\"; filename=\"") + IMG_FILE_NAME + "\"\r\n";
  multipart_header += "\r\n";
  // create multipart footer
  String multipart_footer = "";
  multipart_footer += "\r\n";
  multipart_footer += String("--") + HTTP_BOUNDARY + "--\r\n";
  multipart_footer += "\r\n";
  // concatenate data
  uint32_t multipart_header_size = multipart_header.length();
  uint32_t multipart_footer_size = multipart_footer.length();
  uint32_t multipart_all_size = multipart_header_size + IMG_FILE_SIZE + multipart_footer_size;
  Serial.println("send " + String(multipart_all_size) + " bytes");
  uint8_t* multipart_data = (uint8_t*)malloc(sizeof(uint8_t) * multipart_all_size);
  for (int i = 0; i < multipart_header_size; ++i) {
    multipart_data[i] = multipart_header[i];
  }
  for (int i = 0; i < IMG_FILE_SIZE; ++i) {
    multipart_data[multipart_header_size + i] = bmp_img[i];
  }
  for (int i = 0; i < multipart_footer_size; ++i) {
    multipart_data[multipart_header_size + IMG_FILE_SIZE + i] = multipart_footer[i];
  }
  // POST data
  status_code = http.POST(multipart_data, multipart_all_size);
  free(multipart_data);
  if (status_code != HTTP_CODE_OK && status_code != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.println("[ERROR] status code error: " + String(status_code));
    return;
  }
  received_string = http.getString();
  Serial.println(received_string);
  http.end();

  // files.completeUploadExternal
  if (!http.begin(SLACK_URL_COMPLETE_UPLOAD)) {
    Serial.println(String("[ERROR] cannot begin ") + SLACK_URL_COMPLETE_UPLOAD);
    return;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String file_data = "[{\"id\":\"" + String(file_id) + "\", \"title\":\"" + IMG_FILE_NAME + "\"}]";
  body = String("token=") + SLACK_TOKEN + "&channel_id=" + String(SLACK_CHANNEL_ID) + "&files=" + file_data;
  status_code = http.POST(body);
  if (status_code != HTTP_CODE_OK && status_code != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.println("[ERROR] status code error: " + String(status_code));
    return;
  }
  received_string = http.getString();
  Serial.println(received_string);
  http.end();
  /********** upload image **********/
}

void loop() {
}
