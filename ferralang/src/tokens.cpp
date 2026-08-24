#include "tokens.h"

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

void lexInterpolatedString(
  const std::string& str,
  std::vector<Token>& tokens
){
  std::string text;
  bool first = true;

  for(size_t i = 0; i < str.size(); i++){
    if(str[i] == '{'){
      if(!text.empty()){
        if(!first)
          tokens.push_back({CHAR, "+"});

        tokens.push_back({STRING, text});
        text.clear();

        first = false;
      }

      std::string expr;
      i++;

      int depth = 1;

      while(i < str.size() && depth > 0){
        if(str[i] == '{'){
          depth++;
        }
        else if(str[i] == '}'){
          depth--;

          if(depth == 0)
            break;
        }

        if(depth > 0)
          expr += str[i];

        i++;
      }

      if(!first)
        tokens.push_back({CHAR, "+"});

      tokens.push_back({WORD, "tostr"});
      tokens.push_back({CHAR, "("});

      auto exprTokens = tokenize(expr);

      exprTokens.pop_back();

      tokens.insert(
        tokens.end(),
        exprTokens.begin(),
        exprTokens.end()
      );

      tokens.push_back({CHAR, ")"});

      first = false;
    }
    else{
      text += str[i];
    }
  }

  if(!text.empty()){
    if(!first)
      tokens.push_back({CHAR, "+"});

    tokens.push_back({STRING, text});
  }
}

std::vector<Token> tokenize(const std::string& code){
  std::vector<Token> tokens;
  size_t i = 0;
  bool line_break_before_token = false;

  auto emit = [&](TokenType type, std::string value) {
    tokens.push_back({type, std::move(value), line_break_before_token});
    line_break_before_token = false;
  };

  while (i < code.length()) {
    char c = code[i];

    if (std::isspace(static_cast<unsigned char>(c))) { 
      if (c == '\n' || c == '\r') {
        line_break_before_token = true;
      }
      i++;
      continue;
    }
    if (c == '/' && i+1 < code.length() && code[i+1] == '/'){
      i += 2; 
      while (i < code.length() && code[i] != '\n') i++;
      continue;
    }
    if (c == '/' && i+1 < code.length() && code[i+1] == '*') {
      i += 2;
      while (i+1 < code.length() && !(code[i] == '*' && code[i+1] == '/')) {
        if (code[i] == '\n' || code[i] == '\r') {
          line_break_before_token = true;
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

      emit(NUM, std::to_string(strtoull(hex.c_str(), nullptr, 16)));

      continue;
    }
    
    
    static const std::vector<std::string> multi_char_operators = {
      "...",
      "<<=", ">>=",
      "+=", "-=", "*=", "/=", "%=", "&=", "|=", "#=",
      "<=", ">=", "==", "!=",
      "<<", ">>"
    };
    bool matched_operator = false;
    for (const auto& op : multi_char_operators) {
      if (code.compare(i, op.size(), op) == 0) {
        emit(CHAR, op);
        i += op.size();
        matched_operator = true;
        break;
      }
    }
    if (matched_operator) {
      continue;
    }
    if(
      c == '$' &&
      i + 1 < code.length() &&
      (code[i + 1] == '"' || code[i + 1] == '\'')
    ){
      i++;

      std::string str = parseString(code, i);

      const size_t first_interpolated_token = tokens.size();
      lexInterpolatedString(str, tokens);
      if (first_interpolated_token < tokens.size()) {
        tokens[first_interpolated_token].line_break_before = line_break_before_token;
        line_break_before_token = false;
      }

      continue;
    }
    if (c == '\"' || c == '\'') {
      emit(STRING, parseString(code, i));
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

      emit(NUM, num);
      continue;
    }
    if (std::isalpha(c) || c == '_') { 
      std::string word = "";
      while (i < code.length() && (std::isalnum(code[i]) || code[i] == '_')) {
        word += code[i];
        i++;
      }
      emit(WORD, word);
      continue;
    }
    if(c == '=' || c == '(' || c == ')' || c == '{' || c == '}'
      || c == ','  || c == ';' || c == ':' || c == '!' || c == '?'
      || c == '"' || c == '\'' || c == '>' || c == '<' || c == '.'
      || c == '+' || c == '-' || c == '/' || c == '*' || c == '[' || 
      c == ']' || c == '#' || c == '$' || c == '^' || c == '&' || c == '|' ||
      c == '%' || c == '@' || c == '~'
    ){ 
      emit(CHAR, std::string(1, c));
      i++;
      continue;
    }

    std::cout << "NONAME TOKEN: " << c << std::endl;
    i++;  
  }

  emit(CODEEND, "");

  return tokens;
}
