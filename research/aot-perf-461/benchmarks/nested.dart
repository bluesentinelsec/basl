void main() {
  int sum = 0;
  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 1000; j++) {
      sum = (sum + i + j) & 0x7FFFFFFF;
    }
  }
}
