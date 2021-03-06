// file     : estring/enum-parser.cxx
// license  : MIT; see accompanying LICENSE file

#include <estring/enum-parser.hxx>

#include <cctype>
#include <memory>
#include <sstream>

#include <iostream>

#define FOR_EACH_CHAR(var) \
  for (var = next_char (); var != EOF; var = next_char ())

using namespace std;

static const char*
to_string (const cpp_token_type type) noexcept
{
  switch (type)
  {
  case cpp_token_type::close_curly_brace:
    return "cpp_token_type::close_curly_brace";
  case cpp_token_type::colon:
    return "cpp_token_type::colon";
  case cpp_token_type::comma:
    return "cpp_token_type::comma";
  case cpp_token_type::end_of_stream:
    return "cpp_token_type::end_of_stream";
  case cpp_token_type::equals:
    return "cpp_token_type::equals";
  case cpp_token_type::ident:
    return "cpp_token_type::ident";
  case cpp_token_type::integer:
    return "cpp_token_type::integer";
  case cpp_token_type::open_curly_brace:
    return "cpp_token_type::open_curly_brace";
  case cpp_token_type::semicolon:
    return "cpp_token_type::semicolon";
  case cpp_token_type::unknown:
    return "cpp_token_type::unknown";
  }
  return "";
}

unexpected_token::
unexpected_token (const cpp_token_type expected_type,
                  const cpp_token& actual_token) noexcept
{
  ostringstream buf;
  buf << "expected token '" << to_string (expected_type) << "', "
      << "got '" << to_string (actual_token.type) << '\'';

  // The parser only saves the token lexeme (content) if the token type is an
  // identifier, integer or unknown. Otherwise it's undefined.
  //
  if (actual_token.type == cpp_token_type::ident ||
      actual_token.type == cpp_token_type::integer ||
      actual_token.type == cpp_token_type::unknown)
    buf << ": '" << actual_token.lexeme << '\'';

  buf << '\n';
  msg_ = buf.str ();
}

unexpected_token::
unexpected_token (const cpp_token_type expected_type,
                  const std::string& expected_lexeme,
                  const cpp_token& actual_token) noexcept
{
  ostringstream buf;
  buf << "expected token '" << to_string (expected_type) << "': "
      << '\'' << expected_lexeme << "', "
      << "got '" << to_string (actual_token.type) << '\'';

  // See above comment.
  //
  if (actual_token.type == cpp_token_type::ident ||
      actual_token.type == cpp_token_type::integer ||
      actual_token.type == cpp_token_type::unknown)
    buf << ": '" << actual_token.lexeme << '\'';

  buf << '\n';
  msg_ = buf.str ();
}

const char* unexpected_token::
what () const noexcept
{
  return msg_.c_str ();
}

string
generate_code (const vector<enum_info>& enums) noexcept
{
  ostringstream buf;

#define INDENT(size) string(size, ' ')

  for (const auto& e : enums)
  {
    if (e.name.size () > 0)
      buf << "const char* to_string(const " << e.name << " type) noexcept {\n"
          << INDENT (2) << "switch (type) {\n";
    else
      buf << "const char* to_string(const int type) noexcept {\n"
          << INDENT (2) << "switch (type) {\n";

    for (const auto& val : e.values)
    {
      if (e.scoped)
        buf << INDENT (2) << "case " << e.name << "::" << val << ":\n"
            << INDENT (4) << "return \"" << e.name << "::" << val << "\";\n";
      else
        buf << INDENT (2) << "case " << val << ":\n"
            << INDENT (4) << "return \"" << val << "\";\n";
    }

    buf << INDENT (2) << "}\n"
        << INDENT (2) << "return \"\";\n}\n"
        // Print an extra newline after each enum's to_string function except
        // the last one.
        //
        << (e == *(enums.cend () - 1) ? "" : "\n");
  }

#undef INDENT

  // If no enums were passed to the program, be nice to the user and just print
  // a newline.
  //
  if (enums.size () == 0)
    buf << '\n';

  return buf.str ();
}

parse_context::
parse_context (const string& code)
  : buffer_ (code), last_token_size_ {}
{
}

vector<enum_info> parse_context::
parse ()
{
  vector<enum_info> enums;

  for (;;)
  {
    // Since we only care about enums, ignore everything until the enum keyword
    // is found or we've reached the end of input.
    //
    discard_until (cpp_token_type::ident, "enum");

    unique_ptr<cpp_token> token = peek_token ();
    if (token->type == cpp_token_type::end_of_stream)
      break;

    enum_info einfo{};
    token = expect_and_consume (cpp_token_type::ident, "enum");

    // In C++11, enum classes were introduced. Check if either class or struct
    // appears after the enum keyword. We need to remember whether we're dealing
    // with a C enum or a C++11 enum class, since their usage is slightly
    // different and we therefore need to generate different code.
    //
    token = peek_token ();
    if (token->lexeme == "class" || token->lexeme == "struct")
    {
      next_token ();
      einfo.scoped = true;
    }
    else
      einfo.scoped = false;

    // Next we check for the enum's name. Here's we're a bit more flexible than
    // C++ compilers. C++ compilers allow us to omit the name of a C enum if we
    // define it, but not if we don't. For enum classes you always have to name
    // them. To keep our parser simpler, we allow you to always omit the name of
    // an enum. ¯\_(ツ)_/¯
    // 
    token = peek_token ();
    if (token->type == cpp_token_type::ident)
    {
      token = next_token ();
      einfo.name = move (token->lexeme);
    }

    // Since C++11, we can also specify a type that'll be used for enum values.
    // The syntax is a colon (:) followed by the type. In our tokenizer, types
    // are identifiers.
    //
    token = peek_token ();
    if (token->type == cpp_token_type::colon)
    {
      next_token ();
      token = expect_and_consume (cpp_token_type::ident);
      einfo.type = move (token->lexeme);
    }

    // Check for a semicolon (;) here, for cases when we're dealing forward
    // declarations, and move on to the next enum.
    //
    token = peek_token ();
    if (token->type == cpp_token_type::semicolon)
    {
      next_token ();
      continue;
    }

    expect_and_consume (cpp_token_type::open_curly_brace);

    token = peek_token ();
    while (token->type != cpp_token_type::close_curly_brace)
    {
      token = expect_and_consume (cpp_token_type::ident);
      einfo.values.emplace_back (move (token->lexeme));

      // If an enum item has an explicit value, ignore it. We could save the
      // value, but the problem is that C++ also allows compile-time expressions
      // to be values. Therefore we'd have to have an expression evaluator of
      // our own to calculate the values.
      //
      token = peek_token ();
      if (token->type == cpp_token_type::equals)
      {
        for (auto t = peek_token ();
             t->type != cpp_token_type::comma &&
             t->type != cpp_token_type::close_curly_brace;
             t = peek_token ())
          next_token ();
      }

      // Since the comma is optional for the last item in an enum, we only
      // consume the comma if it's used. If the comma isn't used, the next token
      // has to be a close_curly_brace (}), otherwise it's a syntax error.
      //
      token = peek_token ();
      if (token->type == cpp_token_type::comma)
        next_token ();
      else if (token->type != cpp_token_type::close_curly_brace)
        throw unexpected_token (cpp_token_type::close_curly_brace, *token);

      token = peek_token ();
      if (token->type == cpp_token_type::close_curly_brace)
        break;
    }

    token = expect_and_consume (cpp_token_type::close_curly_brace);
    token = expect_and_consume (cpp_token_type::semicolon);

    enums.emplace_back (einfo);
  }

  return enums;
}

bool parse_context::
is_eof () const noexcept
{
  return buffer_.eof ();
}

int parse_context::
prev_char () noexcept
{
  // If we're at the beginning of the buffer, we don't want to call unget,
  // because it'll change internal state and then subsequent operations like
  // peek and get will fail.
  //
  if (buffer_.tellg () != 0)
    buffer_.unget ();
  return buffer_.peek ();
}

int parse_context::
next_char () noexcept
{
  return is_eof () ? EOF : buffer_.get ();
}

void parse_context::
consume_comment () noexcept
{
  int c = next_char ();

  // If the character is a slash, we're dealing with a C++ comment. Consume all
  // characters until either a newline or a carriage return are found.
  //
  if (c == '/')
  {
    FOR_EACH_CHAR (c)
    {
      if (c == '\n' || c == '\r')
        break;
    }
  }
  // If the character is an asterisk, we're dealing with a C comment. Consume
  // all characters until a / is found.
  //
  else if (c == '*')
  {
    FOR_EACH_CHAR (c)
    {
      if (c == '/')
        break;
    }
  }
}

void parse_context::
consume_whitespace () noexcept
{
  int c;
  FOR_EACH_CHAR (c)
  {
    if (!isspace (c))
      break;
  }
  prev_char ();
}

string parse_context::
read_ident () noexcept
{
  int c;
  ostringstream buf;
  last_token_size_ = 0;
  FOR_EACH_CHAR (c)
  {
    if (!isalnum (c) && c != '_' && c != ':')
      break;
    ++last_token_size_;
    buf << static_cast<char> (c);
  }
  prev_char ();
  return buf.str ();
}

string parse_context::
read_int () noexcept
{
  int c;
  ostringstream buf;
  last_token_size_ = 0;
  FOR_EACH_CHAR (c)
  {
    if (!isdigit (c) && !isxdigit (c) && c != 'x')
      break;
    ++last_token_size_;
    buf << static_cast<char> (c);
  }
  prev_char ();
  return buf.str ();
}

unique_ptr<cpp_token> parse_context::
next_token ()
{
  int c;
  unique_ptr<cpp_token> token (new cpp_token);

  FOR_EACH_CHAR (c)
  {
    if (is_eof ())
      break;

    if (c == '}')
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::close_curly_brace;
    }
    else if (c == ':')
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::colon;
    }
    else if (c == ',')
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::comma;
    }
    else if (c == '=')
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::equals;
    }
    // C++ identifiers can only begin with an ASCII alphabetic character or an
    // underscore (_).
    //
    else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_'))
    {
      prev_char ();
      token->type = cpp_token_type::ident;
      token->lexeme = read_ident ();
    }
    else if (c >= '0' && c <= '9')
    {
      prev_char ();
      token->type = cpp_token_type::integer;
      token->lexeme = read_int ();
    }
    else if (c == '{')
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::open_curly_brace;
    }
    else if (c == ';')
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::semicolon;
    }
    else if (c == '/')
    {
      consume_comment ();
      continue;
    }
    else if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
    {
      consume_whitespace ();
      continue;
    }
    else
    {
      last_token_size_ = 1;
      token->type = cpp_token_type::unknown;
      // The unknown character is saved here for the purposes of debugging. If
      // an unexpected_token is thrown and the token type is unknown, we'll be
      // able to see which character was encountered.
      //
      token->lexeme.push_back (static_cast<char> (c));
    }

    return token;
  }

  token->type = cpp_token_type::end_of_stream;
  return token;
}

unique_ptr<cpp_token> parse_context::
peek_token ()
{
  unique_ptr<cpp_token> token = next_token ();
  while (last_token_size_ > 0)
  {
    --last_token_size_;
    prev_char ();
  }
  return token;
}

void parse_context::
discard_until (const cpp_token_type type, const string& lexeme)
{
  for (unique_ptr<cpp_token> token = peek_token ();
       token->type != cpp_token_type::end_of_stream;
       token = peek_token ())
  {
    if (token->type == type && token->lexeme == lexeme)
      break;

    next_token ();
  }
}

unique_ptr<cpp_token> parse_context::
expect_and_consume (const cpp_token_type type)
{
  unique_ptr<cpp_token> token = next_token ();
  if (token->type != type)
    throw unexpected_token (type, *token);
  return token;
}

unique_ptr<cpp_token> parse_context::
expect_and_consume (const cpp_token_type type, const string& lexeme)
{
  unique_ptr<cpp_token> token = next_token ();
  if (token->type != type || token->lexeme != lexeme)
    throw unexpected_token (type, lexeme, *token);
  return token;
}
