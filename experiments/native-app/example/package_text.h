/* Fold provider display names to the same glyph set as the bundled apps.
 * In-place conversion never grows a string. Entity IDs and saved config stay intact. */
static void fold_latin(char *text){
 unsigned read=0,write=0;
 while(text[read]){
  unsigned char first=(unsigned char)text[read];if(first<128){text[write++]=text[read++];continue;}
  unsigned cp=0,count=0;if((first&0xe0)==0xc0&&text[read+1]){cp=((first&31)<<6)|((unsigned char)text[read+1]&63);count=2;}
  else if((first&0xf0)==0xe0&&text[read+1]&&text[read+2]){cp=((first&15)<<12)|(((unsigned char)text[read+1]&63)<<6)|((unsigned char)text[read+2]&63);count=3;}
  else{++read;continue;}
  if(cp==0xc4||cp==0xd6||cp==0xdc||cp==0xe4||cp==0xf6||cp==0xfc||cp==0xdf){for(unsigned i=0;i<count;++i)text[write++]=text[read++];continue;}
  read+=count;char replacement=0;
        if ((cp >= 0x00c0 && cp <= 0x00c5) || cp == 0x0100 || cp == 0x0102 || cp == 0x0104) replacement = 'A';
        else if ((cp >= 0x00e0 && cp <= 0x00e5) || cp == 0x0101 || cp == 0x0103 || cp == 0x0105) replacement = 'a';
        else if (cp == 0x00c7 || cp == 0x0106 || cp == 0x010c) replacement = 'C';
        else if (cp == 0x00e7 || cp == 0x0107 || cp == 0x010d) replacement = 'c';
        else if ((cp >= 0x00c8 && cp <= 0x00cb) || cp == 0x0112 || cp == 0x0118) replacement = 'E';
        else if ((cp >= 0x00e8 && cp <= 0x00eb) || cp == 0x0113 || cp == 0x0119) replacement = 'e';
        else if (cp >= 0x00cc && cp <= 0x00cf) replacement = 'I';
        else if (cp >= 0x00ec && cp <= 0x00ef) replacement = 'i';
        else if (cp == 0x00d1 || cp == 0x0143) replacement = 'N';
        else if (cp == 0x00f1 || cp == 0x0144) replacement = 'n';
        else if ((cp >= 0x00d2 && cp <= 0x00d6) || cp == 0x00d8) replacement = 'O';
        else if ((cp >= 0x00f2 && cp <= 0x00f6) || cp == 0x00f8) replacement = 'o';
        else if (cp >= 0x00d9 && cp <= 0x00dc) replacement = 'U';
        else if (cp >= 0x00f9 && cp <= 0x00fc) replacement = 'u';
        else if (cp == 0x00dd || cp == 0x0178) replacement = 'Y';
        else if (cp == 0x00fd || cp == 0x00ff) replacement = 'y';
        else if (cp == 0x00df) replacement = 's';
        else if (cp == 0x0141) replacement = 'L';
        else if (cp == 0x0142) replacement = 'l';
        else if (cp == 0x015a || cp == 0x0160) replacement = 'S';
        else if (cp == 0x015b || cp == 0x0161) replacement = 's';
        else if (cp == 0x0179 || cp == 0x017b || cp == 0x017d) replacement = 'Z';
        else if (cp == 0x017a || cp == 0x017c || cp == 0x017e) replacement = 'z';
        else if (cp == 0x2013 || cp == 0x2014) replacement = '-';
        else if (cp == 0x2018 || cp == 0x2019) replacement = '\'';
  if(replacement)text[write++]=replacement;
 }text[write]=0;
}
