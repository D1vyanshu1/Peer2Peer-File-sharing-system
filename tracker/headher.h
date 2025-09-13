#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
using namespace std;

pair<string, int> read_info(const string &filename, int n);