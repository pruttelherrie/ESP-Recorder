/*
 * This is library to serve files from an SD-card using Me No Dev's async webserver for the ESP8266.
 * See https://github.com/me-no-dev/ESPAsyncWebServer
 *
 * Adapted from an example by pim-borst, https://gist.github.com/pim-borst/17934bfd4454caea3ba4f74366c2135c
 *
 * Notes: 
 * In your main script you must specific #define FS_NO_GLOBALS, otherwise there is a namespace conflict between the SPIFFS and SD "File" type
 * Files and directories being served from the SD must follow an 8dot3 naming scheme, so no "html" files only "htm"
 */

#include <FS.h>
#include <ESPAsyncWebServer.h>
#include "AsyncSDServer.h"
#include <SD.h>

//bool SD_exists(fs::FS &fs, const char* path) {
bool SD_exists(fs::FS &fs, String path) {

//bool SD_exists(SDClass &sd, String path) {
  // For some reason SD.exists(filename) reboots the ESP...
  // So we test by opening the file
  bool exists = false;
  File test = fs.open(path);
  if(test){
    test.close();
    exists = true;
  }
  return exists;
}

AsyncSDIndexResponse::~AsyncSDIndexResponse(){
  if(_content)
    _content.close();
}

void AsyncSDIndexResponse::_setContentType(const String& path){
   _contentType = "text/html";
}

AsyncSDIndexResponse::AsyncSDIndexResponse(fs::FS &fs, const String& path, const String& contentType, bool download){
  _code = 200;
  _path = path;
  



  if(contentType == "")
    _setContentType(path);
  else
    _contentType = contentType;


}

AsyncSDIndexResponse::AsyncSDIndexResponse(File content, const String& path, const String& contentType, bool download){
  _code = 200;
  _path = path;

  int filenameStart = path.lastIndexOf('/') + 1;
  char buf[26+path.length()-filenameStart];
  char* filename = (char*)path.c_str() + filenameStart;


  Serial.println("INSIDE THAT OTHER FUNCTION");

  _content = content;
  _contentLength = _content.size();

  if(contentType == "")
    _setContentType(path);
  else
    _contentType = contentType;

  addHeader("Content-Disposition", "inline; filename=\"index.html\"");

}

size_t AsyncSDIndexResponse::_fillBuffer(uint8_t *data, size_t len){
  _content.read(data, len);
  return len;
}

AsyncSDFileResponse::~AsyncSDFileResponse(){
  if(_content)
    _content.close();
}

void AsyncSDFileResponse::_setContentType(const String& path){
  if (path.endsWith(".html")) _contentType = "text/html";
  else if (path.endsWith(".htm")) _contentType = "text/html";
  else if (path.endsWith(".css")) _contentType = "text/css";
  else if (path.endsWith(".json")) _contentType = "text/json";
  else if (path.endsWith(".js")) _contentType = "application/javascript";
  else if (path.endsWith(".png")) _contentType = "image/png";
  else if (path.endsWith(".gif")) _contentType = "image/gif";
  else if (path.endsWith(".jpg")) _contentType = "image/jpeg";
  else if (path.endsWith(".ogg")) _contentType = "audio/ogg";
  else if (path.endsWith(".OGG")) _contentType = "audio/ogg";
  else if (path.endsWith(".ico")) _contentType = "image/x-icon";
  else if (path.endsWith(".svg")) _contentType = "image/svg+xml";
  else if (path.endsWith(".eot")) _contentType = "font/eot";
  else if (path.endsWith(".woff")) _contentType = "font/woff";
  else if (path.endsWith(".woff2")) _contentType = "font/woff2";
  else if (path.endsWith(".ttf")) _contentType = "font/ttf";
  else if (path.endsWith(".xml")) _contentType = "text/xml";
  else if (path.endsWith(".pdf")) _contentType = "application/pdf";
  else if (path.endsWith(".zip")) _contentType = "application/zip";
  else if(path.endsWith(".gz")) _contentType = "application/x-gzip";
  else _contentType = "text/plain";
}

AsyncSDFileResponse::AsyncSDFileResponse(fs::FS &fs, const String& path, const String& contentType, bool download){
  _code = 200;
  _path = path;
  
  if(!download && !SD_exists(fs, _path) && SD_exists(fs, _path+".gz")){
    _path = _path+".gz";
    addHeader("Content-Encoding", "gzip");
  }

  _content = fs.open(_path, FILE_READ);
  _contentLength = _content.size();
  _sourceIsValid = _content;

  if(contentType == "")
    _setContentType(path);
  else
    _contentType = contentType;

  int filenameStart = path.lastIndexOf('/') + 1;
  char buf[26+path.length()-filenameStart];
  char* filename = (char*)path.c_str() + filenameStart;

  if(download) {
    // set filename and force download
    snprintf(buf, sizeof (buf), "attachment; filename=\"%s\"", filename);
  } else {
    // set filename and force rendering
    snprintf(buf, sizeof (buf), "inline; filename=\"%s\"", filename);
  }
  addHeader("Content-Disposition", buf);
}

AsyncSDFileResponse::AsyncSDFileResponse(File content, const String& path, const String& contentType, bool download){
  _code = 200;
  _path = path;
  _content = content;
  _contentLength = _content.size();

  if(!download && String(_content.name()).endsWith(".gz") && !path.endsWith(".gz"))
    addHeader("Content-Encoding", "gzip");

  if(contentType == "")
    _setContentType(path);
  else
    _contentType = contentType;

  int filenameStart = path.lastIndexOf('/') + 1;
  char buf[26+path.length()-filenameStart];
  char* filename = (char*)path.c_str() + filenameStart;

  Serial.print("SENDING FILE: ");
  Serial.println(filename);
  snprintf(buf, sizeof (buf), "attachment; filename=\"%s\"", filename);
  addHeader("Content-Disposition", buf);
}

size_t AsyncSDFileResponse::_fillBuffer(uint8_t *data, size_t len){
  _content.read(data, len);
  return len;
}

AsyncStaticSDWebHandler::AsyncStaticSDWebHandler(const char* uri, fs::FS& fs, const char* path, const char* cache_control)
  : _fs(fs), _uri(uri), _path(path), _default_file("index.htm"), _cache_control(cache_control), _last_modified("")
{
  // Ensure leading '/'
  if (_uri.length() == 0 || _uri[0] != '/') _uri = "/" + _uri;
  if (_path.length() == 0 || _path[0] != '/') _path = "/" + _path;

  // If path ends with '/' we assume a hint that this is a directory to improve performance.
  // However - if it does not end with '/' we, can't assume a file, path can still be a directory.
  _isDir = _path[_path.length()-1] == '/';

  // Remove the trailing '/' so we can handle default file
  // Notice that root will be "" not "/"
  if (_uri[_uri.length()-1] == '/') _uri = _uri.substring(0, _uri.length()-1);
  if (_path[_path.length()-1] == '/') _path = _path.substring(0, _path.length()-1);

  // Reset stats
  _gzipFirst = false;
  _gzipStats = 0xF8;
}

AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setIsDir(bool isDir){
  _isDir = isDir;
  return *this;
}

AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setDefaultFile(const char* filename){
  _default_file = String(filename);
  return *this;
}

AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setCacheControl(const char* cache_control){
  _cache_control = String(cache_control);
  return *this;
}

AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setLastModified(const char* last_modified){
  _last_modified = String(last_modified);
  return *this;
}

AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setLastModified(struct tm* last_modified){
  char result[30];
  strftime (result,30,"%a, %d %b %Y %H:%M:%S %Z", last_modified);
  return setLastModified((const char *)result);
}
#ifdef ESP8266
AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setLastModified(time_t last_modified){
  return setLastModified((struct tm *)gmtime(&last_modified));
}

AsyncStaticSDWebHandler& AsyncStaticSDWebHandler::setLastModified(){
  time_t last_modified;
  if(time(&last_modified) == 0) //time is not yet set
    return *this;
  return setLastModified(last_modified);
}
#endif
bool AsyncStaticSDWebHandler::canHandle(AsyncWebServerRequest *request){
  if (request->method() == HTTP_GET &&
      request->url().startsWith(_uri) &&
      _getFile(request)) {

    // We interested in "If-Modified-Since" header to check if file was modified
    if (_last_modified.length())
      request->addInterestingHeader("If-Modified-Since");

    if(_cache_control.length())
      request->addInterestingHeader("If-None-Match");

    DEBUGF("[AsyncStaticSDWebHandler::canHandle] TRUE\n");
    return true;
  }

  return false;
}

bool AsyncStaticSDWebHandler::_getFile(AsyncWebServerRequest *request)
{
  // Remove the found uri
  String path = request->url().substring(_uri.length());

  // We can skip the file check and look for default if request is to the root of a directory or that request path ends with '/'
  bool canSkipFileCheck = (_isDir && path.length() == 0) || (path.length() && path[path.length()-1] == '/');

  path = _path + path;

  // Do we have a file or .gz file
  if (!canSkipFileCheck && _fileExists(request, path))
    return true;

  // Can't handle if not default file
  if (_default_file.length() == 0)
    return false;

  // Try to add default file, ensure there is a trailing '/' ot the path.
  if (path.length() == 0 || path[path.length()-1] != '/')
    path += "/";
  path += _default_file;

  return _fileExists(request, path);
}

bool AsyncStaticSDWebHandler::_fileExists(AsyncWebServerRequest *request, const String& path)
{
  bool fileFound = false;
  bool gzipFound = false;

  String gzip = path + ".gz";

  // Following part reworked to use SD_exists instead of request->_tempFile = _fs.open()
  // Drawback: AsyncSDFileResponse(sd::File content, ...) cannot be used so
  //            file needs to be opened again in AsyncSDFileResponse(SDClass &sd, ...)
  //            request->_tempFile is of wrong fs::File type anyway...
  if (_gzipFirst) {
    gzipFound = SD_exists(_fs, gzip);
    if (!gzipFound){
      fileFound = SD_exists(_fs, path);
    }
  } else {
    fileFound = SD_exists(_fs, path);
    if (!fileFound){
      gzipFound = SD_exists(_fs, gzip);
    }
  }

  bool found = fileFound || gzipFound;

  if (found) {
    // Extract the file name from the path and keep it in _tempObject
    size_t pathLen = path.length();
    char * _tempPath = (char*)malloc(pathLen+1);
    snprintf(_tempPath, pathLen+1, "%s", path.c_str());
    request->_tempObject = (void*)_tempPath;

    // Calculate gzip statistic
    _gzipStats = (_gzipStats << 1) + (gzipFound ? 1 : 0);
    if (_gzipStats == 0x00) _gzipFirst = false; // All files are not gzip
    else if (_gzipStats == 0xFF) _gzipFirst = true; // All files are gzip
    else _gzipFirst = _countBits(_gzipStats) > 4; // IF we have more gzip files - try gzip first
  }

  return found;
}

uint8_t AsyncStaticSDWebHandler::_countBits(const uint8_t value) const
{
  uint8_t w = value;
  uint8_t n;
  for (n=0; w!=0; n++) w&=w-1;
  return n;
}

void AsyncStaticSDWebHandler::handleRequest(AsyncWebServerRequest *request)
{
  // Get the filename from request->_tempObject and free it
  String filename = String((char*)request->_tempObject);
  free(request->_tempObject);
  request->_tempObject = NULL;

  Serial.print("8===>");
  Serial.print(filename);
  Serial.println("<===8");

  if(filename.compareTo("/index.htm") == 0) { // Get SD contents into an html file
    Serial.println("Getting SD index for you, Iwan.");
    AsyncWebServerResponse * response = new AsyncSDIndexResponse(/* request->_tempFile, */ _fs, filename);

    String p;
    p.concat("<html><body>");

    File entry;
    File dir = SD.open("/");
    dir.rewindDirectory();
    //Serial.print("ROOT DIRECTORY:");
    //Serial.print("\r\n");
    while(true) {
        entry = dir.openNextFile();
        if (!entry) break;

        if(strstr(entry.name(),".OGG") > 0) {
            //if(strcmp(entry.name(), ".OGG")) {
            p.concat("<a href='");
            p.concat(entry.name()+1);
            p.concat("'>");
            p.concat(entry.name()+1);
            p.concat(" (");
            p.concat(entry.size());
            p.concat(" bytes)");
            p.concat("</a><br>\n");
            //Serial.printf("%s, type: %s, size: %ld", entry.name(), (entry.isDirectory())?"dir":"file", entry.size());
            entry.close();
            //Serial.print("\r\n");
        }
    }
    dir.close();
    p.concat("</body></html>\n");
    request->send(200, "text/html", p.c_str());

  } else {
    // Serve file
    File _tempFile = _fs.open(filename);
    if (_tempFile == true) {
     String etag = String(_tempFile.size());
     _tempFile.close();    

      // Cannot use new AsyncSDFileResponse(request->_tempFile, ...) here because request->_tempFile has not be opened
      // in AsyncStaticSDWebHandler::_fileExists and is of wrong type fs::File anyway.
      AsyncWebServerResponse * response = new AsyncSDFileResponse(/* request->_tempFile, */ _fs, filename);
      request->send(response);

      //AsyncWebServerResponse * AsyncWebServerRequest::beginResponse(int code, const String& contentType, const String& content){
      // def in WebResponses.cpp, #181
     
    } else {
      request->send(404);
    }
  }
}

