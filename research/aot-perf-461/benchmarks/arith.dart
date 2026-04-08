void main() {
  int sum = 0;
  for (int i = 0; i < 100000000; i++) {
    sum = sum + i * 3 - i ~/ 2;
  }
}
