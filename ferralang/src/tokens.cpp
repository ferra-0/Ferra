#include "tokens.h"

int gline = 0;

std::string parseString(const std::string& code, size_t& i) {
  std::string out;
  char quote = code[i];
  i++; 

  while (i < code.size()) {
    char c = code[i];

    if (c == quote) {
      i++;
      return out;
    }

    if (c == '\\') {
      i++;
      if (i >= code.size()) break;

      char esc = code[i];

      switch (esc) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '0': out += '\0'; break;
        case '"': out += '"'; break;
        case 'e': out += '\x1b'; break;
        case '\'': out += '\''; break;
        case '\\': out += '\\'; break;
        default: out += esc; break;
      }

      i++;
    } else {
      out += c;
      i++;
    }
  }

  throw std::runtime_error("Unterminated string");
}

static const std::vector<std::string> multi_char_operators = {
  "...",
  "<<=", ">>=",
  "+=", "-=", "*=", "/=", "%=", "&=", "|=", "#=",
  "<=", ">=", "==", "!=",
  "<<", ">>"
};

std::vector<Token> tokenize(const std::string& code){
  std::vector<Token> tokens;
  size_t i = 0;
  bool line_break_before_token = false;
  gline = 1;

  auto emit = [&](TokenType type, std::string value) {
    tokens.push_back({type, std::move(value), line_break_before_token, gline});
    line_break_before_token = false;
  };

  while (i < code.length()) {
    char c = code[i];

    if (std::isspace(static_cast<unsigned char>(c))) {
      if (c == '\r' || c == '\n') {
        line_break_before_token = true;
        gline++;
        if (c == '\r' && i + 1 < code.length() && code[i + 1] == '\n') {
          i += 2;
          continue;
        }
      }
      i++;
      continue;
    }
    if (c == '/' && i+1 < code.length() && code[i+1] == '/'){
      i += 2; 
      while (i < code.length() && code[i] != '\n') {
        i++;
      }
      continue;
    }
    if (c == '/' && i+1 < code.length() && code[i+1] == '*') {
      i += 2;
      while (i+1 < code.length() && !(code[i] == '*' && code[i+1] == '/')) {
        if (code[i] == '\r' || code[i] == '\n') {
          line_break_before_token = true;
          gline++;
          if (code[i] == '\r' && i + 1 < code.length() &&
              code[i + 1] == '\n') {
            i++;
          }
        }
        i++;
      }
      i += 2; 
      continue;
    }
    if (
      c == '0' &&
      i + 1 < code.length() &&
      (code[i + 1] == 'x' || code[i + 1] == 'X')){
      i += 2; 

      std::string hex;

      while (
        i < code.length() &&
        std::isxdigit((unsigned char)code[i])
      ) {
        hex += code[i];
        i++;
      }

      emit(TNUM, std::to_string(strtoull(hex.c_str(), nullptr, 16)));

      continue;
    }
    
    bool matched_operator = false;
    for (const auto& op : multi_char_operators) {
      if (code.compare(i, op.size(), op) == 0) {
        emit(T_CHAR, op);
        i += op.size();
        matched_operator = true;
        break;
      }
    }
    if (matched_operator) {
      continue;
    }
    if (c == '\"' || c == '\'') {
      emit(TSTRING, parseString(code, i));
      continue;
    }
    if (std::isdigit(c) || (c == '.' && std::isdigit(code[i+1]))) { 
      std::string num = "";
      bool hasdot = false;

      while (i < code.length()) {
        if(std::isdigit(code[i])){
          num += code[i];
        }else if(code[i] == '.' && !hasdot){ 
          hasdot = true;
          num += '.';
        }else{
          break;
        }

        i++;
      }

      emit(TNUM, num);
      continue;
    }
    if (std::isalpha(c) || c == '_') { 
      std::string word = "";
      while (i < code.length() && (std::isalnum(code[i]) || code[i] == '_')) {
        word += code[i];
        i++;
      }
      emit(TWORD, word);
      continue;
    }
    if(c == '=' || c == '(' || c == ')' || c == '{' || c == '}'
      || c == ','  || c == ';' || c == ':' || c == '!' || c == '?'
      || c == '"' || c == '\'' || c == '>' || c == '<' || c == '.'
      || c == '+' || c == '-' || c == '/' || c == '*' || c == '[' || 
      c == ']' || c == '#' || c == '$' || c == '^' || c == '&' || c == '|' ||
      c == '%' || c == '@' || c == '~'
    ){ 
      emit(T_CHAR, std::string(1, c));
      i++;
      continue;
    }

    std::cout << "NONAME TOKEN: " << c << std::endl;
    i++;  
  }

  emit(TCODEEND, "");

  return tokens;
}
