#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <thread>

enum TokenType {
	WORD, CHAR, NUM, FLOAT, CODEEND, STRING, DIRECTIVE
};

struct Token {
  TokenType type;
  std::string value;
  
  
  
  bool line_break_before = false;

  std::string tostr() const {
    switch(type){
      case WORD: return "WORD"; break;
      case CHAR: return "CHAR"; break;
      case NUM: return "NUM"; break;
      case FLOAT: return "FLOAT"; break;
      case CODEEND: return "CODEEND"; break;
      case STRING: return "STRING"; break;
      default: return "UNDEFINED"; break;
    }
  }
};

std::vector<Token> tokenize(const std::string& code);
