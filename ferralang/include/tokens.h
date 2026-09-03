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
	TWORD, T_CHAR, TNUM, TFLOAT, TCODEEND, TSTRING, TDIRECTIVE
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
      case TWORD: return "TWORD"; break;
      case T_CHAR: return "T_CHAR"; break;
      case TNUM: return "TNUM"; break;
      case TFLOAT: return "TFLOAT"; break;
      case TCODEEND: return "TCODEEND"; break;
      case TSTRING: return "TSTRING"; break;
      default: return "UNDEFINED"; break;
    }
  }
};

std::vector<Token> tokenize(const std::string& code);
