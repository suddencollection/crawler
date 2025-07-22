#include <main.hpp>
#include <program.hpp>
#include <exception>
#include <iostream>

int main() {

  try {
    Program program{};
    program.run();
  }
  catch(std::exception e) {
    std::cerr << "[Exception] " << e.what() << std::endl;
  }
}
