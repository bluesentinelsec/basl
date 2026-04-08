void main() {
  int x = 0;
  for (int i = 0; i < 10000000; i++) {
    x = x ^ (i & 0xFF);
    x = x | (i >> 3);
    x = x & 0x7FFFFFFF;
  }
}
