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
#include <utility>

enum TokenType {
	WORD, CHAR, NUM, FLOAT, CODEEND, STRING, DIRECTIVE
};

extern int gline;

struct Token {
  TokenType type;
  std::string value;
  
  bool line_break_before = false;

  int line = 0;

  Token(TokenType token_type, std::string token_value,
        bool has_line_break = false, int source_line = 0)
      : type(token_type), value(std::move(token_value)),
        line_break_before(has_line_break), line(source_line) {}

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
