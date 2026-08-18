#include "bim/ifc_spatial_tree.h"

#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: tamias_ifc_dump <file.ifc>\n";
    return 1;
  }
  auto tree = tamias::format_ifc_spatial_tree(argv[1]);
  if (!tree) {
    std::cerr << tree.error() << '\n';
    return 1;
  }
  std::cout << *tree;
  return 0;
}
