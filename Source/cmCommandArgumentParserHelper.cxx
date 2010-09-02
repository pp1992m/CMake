/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmCommandArgumentParserHelper.h"

#include "cmSystemTools.h"
#include "cmCommandArgumentLexer.h"

#include "cmMakefile.h"

int cmCommandArgument_yyparse( yyscan_t yyscanner );
//
cmCommandArgumentParserHelper::cmCommandArgumentParserHelper()
{
  this->WarnUninitialized = false;
  this->CheckSystemVars = false;
  this->FileLine = -1;
  this->FileName = 0;
  this->RemoveEmpty = true;
  this->EmptyVariable[0] = 0;
  strcpy(this->DCURLYVariable, "${");
  strcpy(this->RCURLYVariable, "}");
  strcpy(this->ATVariable,     "@");
  strcpy(this->DOLLARVariable, "$");
  strcpy(this->LCURLYVariable, "{");
  strcpy(this->BSLASHVariable, "\\");

  this->NoEscapeMode = false;
  this->ReplaceAtSyntax = false;
}


cmCommandArgumentParserHelper::~cmCommandArgumentParserHelper()
{
  this->CleanupParser();
}

void cmCommandArgumentParserHelper::SetLineFile(long line, const char* file)
{
  this->FileLine = line;
  this->FileName = file;
}

char* cmCommandArgumentParserHelper::AddString(const char* str)
{
  if ( !str || !*str )
    {
    return this->EmptyVariable;
    }
  char* stVal = new char[strlen(str)+1];
  strcpy(stVal, str);
  this->Variables.push_back(stVal);
  return stVal;
}

char* cmCommandArgumentParserHelper::ExpandSpecialVariable(const char* key, 
                                                           const char* var)
{
  if ( !key )
    {
    return this->ExpandVariable(var);
    }
  if(!var)
    {
    return this->EmptyVariable;
    }
  if ( strcmp(key, "ENV") == 0 )
    {
    char *ptr = getenv(var);
    if (ptr)
      {
      if (this->EscapeQuotes)
        {
        return this->AddString(cmSystemTools::EscapeQuotes(ptr).c_str());
        }
      else
        {
        return ptr;
        }
      }
    return this->EmptyVariable;
    }
  if ( strcmp(key, "CACHE") == 0 )
    {
    if(const char* c = this->Makefile->GetCacheManager()->GetCacheValue(var))
      {
      if(this->EscapeQuotes)
        {
        return this->AddString(cmSystemTools::EscapeQuotes(c).c_str());
        }
      else
        {
        return this->AddString(c);
        }
      }
    return this->EmptyVariable;
    }
  cmOStringStream e;
  e << "Syntax $" << key << "{} is not supported.  "
    << "Only ${}, $ENV{}, and $CACHE{} are allowed.";
  this->SetError(e.str());
  return 0;
}

char* cmCommandArgumentParserHelper::ExpandVariable(const char* var)
{
  if(!var)
    {
    return 0;
    }
  if(this->FileLine >= 0 && strcmp(var, "CMAKE_CURRENT_LIST_LINE") == 0)
    {
    cmOStringStream ostr;
    ostr << this->FileLine;
    return this->AddString(ostr.str().c_str());
    }
  const char* value = this->Makefile->GetDefinition(var);
  if(!value && !this->RemoveEmpty)
    {
    // check to see if we need to print a warning
    // if strict mode is on and the variable has
    // not been "cleared"/initialized with a set(foo ) call
    if(this->WarnUninitialized && !this->Makefile->VariableInitialized(var))
      {
      const char* srcRoot = this->Makefile->GetDefinition("CMAKE_SOURCE_DIR");
      const char* binRoot = this->Makefile->GetDefinition("CMAKE_BINARY_DIR");
      if (this->CheckSystemVars || strstr(this->FileName, srcRoot) == this->FileName ||
          strstr(this->FileName, binRoot) == this->FileName)
        {
        cmOStringStream msg;
        msg << this->FileName << ":" << this->FileLine << ":" <<
          " warning: uninitialized variable \'" << var << "\'";
        cmSystemTools::Message(msg.str().c_str());
        }
      }
    return 0;
    }
  if (this->EscapeQuotes && value)
    {
    return this->AddString(cmSystemTools::EscapeQuotes(value).c_str());
    }
  return this->AddString(value);
}

char* cmCommandArgumentParserHelper::ExpandVariableForAt(const char* var)
{
  if(this->ReplaceAtSyntax)
    {
    // try to expand the variable
    char* ret = this->ExpandVariable(var);
    // if the return was 0 and we want to replace empty strings
    // then return an empty string 
    if(!ret && this->RemoveEmpty)
      {
      return this->AddString(ret);
      }
    // if the ret was not 0, then return it
    if(ret)
      {
      return ret;
      }
    }
  // at this point we want to put it back because of one of these cases:
  // - this->ReplaceAtSyntax is false  
  // - this->ReplaceAtSyntax is true, but this->RemoveEmpty is false,
  //   and the variable was not defined
  std::string ref = "@";
  ref += var;
  ref += "@";
  return this->AddString(ref.c_str());
}

char* cmCommandArgumentParserHelper::CombineUnions(char* in1, char* in2)
{
  if ( !in1 )
    {
    return in2;
    }
  else if ( !in2 )
    {
    return in1;
    }
  size_t len = strlen(in1) + strlen(in2) + 1;
  char* out = new char [ len ];
  strcpy(out, in1);
  strcat(out, in2);
  this->Variables.push_back(out);
  return out;
}

void cmCommandArgumentParserHelper::AllocateParserType
(cmCommandArgumentParserHelper::ParserType* pt,const char* str, int len)
{
  pt->str = 0;
  if ( len == 0 )
    {
    len = static_cast<int>(strlen(str));
    }
  if ( len == 0 )
    {
    return;
    }
  pt->str = new char[ len + 1 ];
  strncpy(pt->str, str, len);
  pt->str[len] = 0;
  this->Variables.push_back(pt->str);
}

bool cmCommandArgumentParserHelper::HandleEscapeSymbol
(cmCommandArgumentParserHelper::ParserType* pt, char symbol)
{
  switch ( symbol )
    {
  case '\\':
  case '"':
  case ' ':
  case '#':
  case '(':
  case ')':
  case '$':
  case '@':
  case '^':
    this->AllocateParserType(pt, &symbol, 1);
    break;
  case ';':
    this->AllocateParserType(pt, "\\;", 2);
    break;
  case 't':
    this->AllocateParserType(pt, "\t", 1);
    break;
  case 'n':
    this->AllocateParserType(pt, "\n", 1);
    break;
  case 'r':
    this->AllocateParserType(pt, "\r", 1);
    break;
  case '0':
    this->AllocateParserType(pt, "\0", 1);
    break;
  default:
    {
    cmOStringStream e;
    e << "Invalid escape sequence \\" << symbol;
    this->SetError(e.str());
    }
    return false;
    }
  return true;
}

void cmCommandArgument_SetupEscapes(yyscan_t yyscanner, bool noEscapes);

int cmCommandArgumentParserHelper::ParseString(const char* str, int verb)
{
  if ( !str)
    {
    return 0;
    }
  this->Verbose = verb;
  this->InputBuffer = str;
  this->InputBufferPos = 0;
  this->CurrentLine = 0;
  
  this->Result = "";

  yyscan_t yyscanner;
  cmCommandArgument_yylex_init(&yyscanner);
  cmCommandArgument_yyset_extra(this, yyscanner);
  cmCommandArgument_SetupEscapes(yyscanner, this->NoEscapeMode);
  int res = cmCommandArgument_yyparse(yyscanner);
  cmCommandArgument_yylex_destroy(yyscanner);
  if ( res != 0 )
    {
    return 0;
    }

  this->CleanupParser();

  if ( Verbose )
    {
    std::cerr << "Expanding [" << str << "] produced: [" 
              << this->Result.c_str() << "]" << std::endl;
    }
  return 1;
}

void cmCommandArgumentParserHelper::CleanupParser()
{
  std::vector<char*>::iterator sit;
  for ( sit = this->Variables.begin();
    sit != this->Variables.end();
    ++ sit )
    {
    delete [] *sit;
    }
  this->Variables.erase(this->Variables.begin(), this->Variables.end());
}

int cmCommandArgumentParserHelper::LexInput(char* buf, int maxlen)
{
  if ( maxlen < 1 )
    {
    return 0;
    }
  if ( this->InputBufferPos < this->InputBuffer.size() )
    {
    buf[0] = this->InputBuffer[ this->InputBufferPos++ ];
    if ( buf[0] == '\n' )
      {
      this->CurrentLine ++;
      }
    return(1);
    }
  else
    {
    buf[0] = '\n';
    return( 0 );
    }
}

void cmCommandArgumentParserHelper::Error(const char* str)
{
  unsigned long pos = static_cast<unsigned long>(this->InputBufferPos);
  cmOStringStream ostr;
  ostr << str << " (" << pos << ")";
  this->SetError(ostr.str());
}

void cmCommandArgumentParserHelper::SetMakefile(const cmMakefile* mf)
{
  this->Makefile = mf;
  this->WarnUninitialized = mf->GetCMakeInstance()->GetWarnUninitialized();
  this->CheckSystemVars = mf->GetCMakeInstance()->GetCheckSystemVars();
}

void cmCommandArgumentParserHelper::SetResult(const char* value)
{
  if ( !value )
    {
    this->Result = "";
    return;
    }
  this->Result = value;
}

void cmCommandArgumentParserHelper::SetError(std::string const& msg)
{
  // Keep only the first error.
  if(this->ErrorString.empty())
    {
    this->ErrorString = msg;
    }
}
