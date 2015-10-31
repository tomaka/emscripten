//
// WebAssembly representation and processing library
//

#ifndef __wasm_h__
#define __wasm_h__

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "colors.h"

namespace wasm {

// Utilities

// Arena allocation for mixed-type data.
struct Arena {
  std::vector<char*> chunks;
  int index; // in last chunk

  template<class T>
  T* alloc() {
    const size_t CHUNK = 10000;
    size_t currSize = (sizeof(T) + 7) & (-8); // same alignment as malloc TODO optimize?
    assert(currSize < CHUNK);
    if (chunks.size() == 0 || index + currSize >= CHUNK) {
      chunks.push_back(new char[CHUNK]);
      index = 0;
    }
    T* ret = (T*)(chunks.back() + index);
    index += currSize;
    new (ret) T();
    return ret;
  }

  void clear() {
    for (char* chunk : chunks) {
      delete[] chunk;
    }
    chunks.clear();
  }

  ~Arena() {
    clear();
  }
};

std::ostream &doIndent(std::ostream &o, unsigned indent) {
  for (unsigned i = 0; i < indent; i++) {
    o << "  ";
  }
  return o;
}
std::ostream &incIndent(std::ostream &o, unsigned& indent) {
  o << '\n';
  indent++;
  return o; 
}
std::ostream &decIndent(std::ostream &o, unsigned& indent) {
  indent--;
  doIndent(o, indent);
  return o << ')';
}

// Basics

struct Name : public cashew::IString {
  Name() : cashew::IString() {}
  Name(const char *str) : cashew::IString(str) {}
  Name(cashew::IString str) : cashew::IString(str) {}

  friend std::ostream& operator<<(std::ostream &o, Name name) {
    assert(name.str);
    return o << '$' << name.str; // reference interpreter requires we prefix all names
  }
};

// Types

enum WasmType {
  none,
  i32,
  i64,
  f32,
  f64
};

const char* printWasmType(WasmType type) {
  switch (type) {
    case WasmType::none: return "none";
    case WasmType::i32: return "i32";
    case WasmType::i64: return "i64";
    case WasmType::f32: return "f32";
    case WasmType::f64: return "f64";
  }
}

unsigned getWasmTypeSize(WasmType type) {
  switch (type) {
    case WasmType::none: abort();
    case WasmType::i32: return 4;
    case WasmType::i64: return 8;
    case WasmType::f32: return 4;
    case WasmType::f64: return 8;
  }
}

bool isFloat(WasmType type) {
  switch (type) {
    case f32:
    case f64: return true;
  }
  return false;
}

WasmType getWasmType(unsigned size, bool float_) {
  if (size < 4) return WasmType::i32;
  if (size == 4) return float_ ? WasmType::f32 : WasmType::i32;
  if (size == 8) return float_ ? WasmType::f64 : WasmType::i64;
  abort();
}

std::ostream &prepareMajorColor(std::ostream &o) {
  Colors::red(o);
  Colors::bold(o);
  return o;
}

std::ostream &prepareColor(std::ostream &o) {
  Colors::magenta(o);
  Colors::bold(o);
  return o;
}

std::ostream &prepareMinorColor(std::ostream &o) {
  Colors::orange(o);
  return o;
}

std::ostream &restoreNormalColor(std::ostream &o) {
  Colors::normal(o);
  return o;
}

std::ostream& printText(std::ostream &o, const char *str) {
  o << '"';
  Colors::green(o);
  o << str;
  Colors::normal(o);
  return o << '"';
}

struct Literal {
  WasmType type;
  union {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  };

  Literal() : type(WasmType::none) {}
  Literal(int32_t init) : type(WasmType::i32), i32(init) {}
  Literal(int64_t init) : type(WasmType::i64), i64(init) {}
  Literal(float   init) : type(WasmType::f32), f32(init) {}
  Literal(double  init) : type(WasmType::f64), f64(init) {}

  void printDouble(std::ostream &o, double d) {
    const char *text = JSPrinter::numToString(d);
    // spec interpreter hates floats starting with '.'
    if (text[0] == '.') {
      o << '0';
    } else if (text[0] == '-' && text[1] == '.') {
      o << "-0";
      text++;
    }
    o << text;
  }

  friend std::ostream& operator<<(std::ostream &o, Literal literal) {
    o << '(';
    prepareMinorColor(o) << printWasmType(literal.type) << ".const ";
    switch (literal.type) {
      case none: abort();
      case WasmType::i32: o << literal.i32; break;
      case WasmType::i64: o << literal.i64; break;
      case WasmType::f32: literal.printDouble(o, literal.f32); break;
      case WasmType::f64: literal.printDouble(o, literal.f64); break;
    }
    restoreNormalColor(o);
    return o << ')';
  }
};

// Operators

enum UnaryOp {
  Clz, Ctz, Popcnt, // int
  Neg, Abs, Ceil, Floor, Trunc, Nearest, Sqrt // float
};

enum BinaryOp {
  Add, Sub, Mul, // int or float
  DivS, DivU, RemS, RemU, And, Or, Xor, Shl, ShrU, ShrS, // int
  Div, CopySign, Min, Max // float
};

enum RelationalOp {
  Eq, Ne, // int or float
  LtS, LtU, LeS, LeU, GtS, GtU, GeS, GeU, // int
  Lt, Le, Gt, Ge // float
};

enum ConvertOp {
  ExtendSInt32, ExtendUInt32, WrapInt64, TruncSFloat32, TruncUFloat32, TruncSFloat64, TruncUFloat64, ReinterpretFloat, // int
  ConvertSInt32, ConvertUInt32, ConvertSInt64, ConvertUInt64, PromoteFloat32, DemoteFloat64, ReinterpretInt // float
};

enum HostOp {
  PageSize, MemorySize, GrowMemory, HasFeature
};

// Expressions

class Expression {
public:
  WasmType type; // the type of the expression: its output, not necessarily its input(s)

  Expression() : type(type) {}

  virtual std::ostream& print(std::ostream &o, unsigned indent) = 0;

  template<class T>
  bool is() {
    return !!dynamic_cast<T*>(this);
  }
};

std::ostream& printFullLine(std::ostream &o, unsigned indent, Expression *expression) {
  doIndent(o, indent);
  expression->print(o, indent);
  o << '\n';
}

std::ostream& printOpening(std::ostream &o, const char *str, bool major=false) {
  o << '(';
  major ? prepareMajorColor(o) : prepareColor(o);
  o << str;
  restoreNormalColor(o);
  return o;
}

std::ostream& printMinorOpening(std::ostream &o, const char *str) {
  o << '(';
  prepareMinorColor(o);
  o << str;
  restoreNormalColor(o);
  return o;
}

typedef std::vector<Expression*> ExpressionList; // TODO: optimize  

class Nop : public Expression {
  std::ostream& print(std::ostream &o, unsigned indent) override {
    return printMinorOpening(o, "nop") << ')';
  }
};

class Block : public Expression {
public:
  Name name;
  ExpressionList list;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "block");
    if (name.is()) {
      o << ' ' << name;
    }
    incIndent(o, indent);
    for (auto expression : list) {
      printFullLine(o, indent, expression);
    }
    return decIndent(o, indent);
  }
};

class If : public Expression {
public:
  Expression *condition, *ifTrue, *ifFalse;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "if");
    incIndent(o, indent);
    printFullLine(o, indent, condition);
    printFullLine(o, indent, ifTrue);
    if (ifFalse) printFullLine(o, indent, ifFalse);
    return decIndent(o, indent);
  }
};

class Loop : public Expression {
public:
  Name out, in;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "loop");
    if (out.is()) {
      o << ' ' << out;
      if (in.is()) {
        o << ' ' << in;
        }
    }
    incIndent(o, indent);
    printFullLine(o, indent, body);
    return decIndent(o, indent);
  }
};

class Label : public Expression {
public:
  Name name;
};

class Break : public Expression {
public:
  Name name;
  Expression *condition, *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "break ") << name;
    incIndent(o, indent);
    if (condition) printFullLine(o, indent, condition);
    if (value) printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Switch : public Expression {
public:
  struct Case {
    Literal value;
    Expression *body;
    bool fallthru;
  };

  Name name;
  Expression *value;
  std::vector<Case> cases;
  Expression *default_;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "switch ") << name;
    incIndent(o, indent);
    printFullLine(o, indent, value);
    o << "TODO: cases/default\n";
    return decIndent(o, indent);
  }

};

class Call : public Expression {
public:
  Name target;
  ExpressionList operands;

  std::ostream& printBody(std::ostream &o, unsigned indent) {
    o << target;
    if (operands.size() > 0) {
      incIndent(o, indent);
      for (auto operand : operands) {
        printFullLine(o, indent, operand);
      }
      decIndent(o, indent);
    } else {
      o << ')';
    }
    return o;
  }

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "call ");
    return printBody(o, indent);
  }
};

class CallImport : public Call {
  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "call_import ");
    return printBody(o, indent);
  }
};

class FunctionType {
public:
  Name name;
  WasmType result;
  std::vector<WasmType> params;

  std::ostream& print(std::ostream &o, unsigned indent, bool full=false) {
    if (full) {
      printOpening(o, "type") << ' ' << name << " (func";
    }
    if (params.size() > 0) {
      o << ' ';
      printMinorOpening(o, "param");
      for (auto& param : params) {
        o << ' ' << printWasmType(param);
      }
      o << ')';
    }
    if (result != none) {
      o << ' ';
      printMinorOpening(o, "result ") << printWasmType(result) << ')';
    }
    if (full) {
      o << "))";;
    }
    return o;
  }

  bool operator==(FunctionType& b) {
    if (name != b.name) return false; // XXX
    if (result != b.result) return false;
    if (params.size() != b.params.size()) return false;
    for (size_t i = 0; i < params.size(); i++) {
      if (params[i] != b.params[i]) return false;
    }
    return true;
  }
  bool operator!=(FunctionType& b) {
    return !(*this == b);
  }
};

class CallIndirect : public Expression {
public:
  FunctionType *type;
  Expression *target;
  ExpressionList operands;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "call_indirect ") << type->name;
    incIndent(o, indent);
    printFullLine(o, indent, target);
    for (auto operand : operands) {
      printFullLine(o, indent, operand);
    }
    return decIndent(o, indent);
  }
};

class GetLocal : public Expression {
public:
  Name id;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    return printOpening(o, "get_local ") << id << ')';
  }
};

class SetLocal : public Expression {
public:
  Name id;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "set_local ") << id;
    incIndent(o, indent);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Load : public Expression {
public:
  unsigned bytes;
  bool signed_;
  bool float_;
  int offset;
  unsigned align;
  Expression *ptr;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    prepareColor(o) << printWasmType(getWasmType(bytes, float_)) << ".load";
    if (bytes < 4) {
      if (bytes == 1) {
        o << '8';
      } else if (bytes == 2) {
        o << "16";
      } else {
        abort();
      }
      o << (signed_ ? "_s" : "_u");
    }
    restoreNormalColor(o);
    o << " align=" << align;
    assert(!offset);
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    return decIndent(o, indent);
  }
};

class Store : public Expression {
public:
  unsigned bytes;
  bool float_;
  int offset;
  unsigned align;
  Expression *ptr, *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    prepareColor(o) << printWasmType(getWasmType(bytes, float_)) << ".store";
    if (bytes < 4) {
      if (bytes == 1) {
        o << '8';
      } else if (bytes == 2) {
        o << "16";
      } else {
        abort();
      }
    }
    restoreNormalColor(o);
    o << " align=" << align;
    assert(!offset);
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Const : public Expression {
public:
  Literal value;

  Const* set(Literal value_) {
    value = value_;
    type = value.type;
    return this;
  }

  std::ostream& print(std::ostream &o, unsigned indent) override {
    return o << value;
  }
};

class Unary : public Expression {
public:
  UnaryOp op;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    prepareColor(o) << printWasmType(type) << '.';
    switch (op) {
      case Clz: o << "clz"; break;
      case Neg: o << "neg"; break;
      case Floor: o << "floor"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Binary : public Expression {
public:
  BinaryOp op;
  Expression *left, *right;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    prepareColor(o) << printWasmType(type) << '.';
    switch (op) {
      case Add:      o << "add"; break;
      case Sub:      o << "sub"; break;
      case Mul:      o << "mul"; break;
      case DivS:     o << "div_s"; break;
      case DivU:     o << "div_u"; break;
      case RemS:     o << "rem_s"; break;
      case RemU:     o << "rem_u"; break;
      case And:      o << "and"; break;
      case Or:       o << "or"; break;
      case Xor:      o << "xor"; break;
      case Shl:      o << "shl"; break;
      case ShrU:     o << "shr_u"; break;
      case ShrS:     o << "shr_s"; break;
      case Div:      o << "div"; break;
      case CopySign: o << "copysign"; break;
      case Min:      o << "min"; break;
      case Max:      o << "max"; break;
      default: abort();
    }
    restoreNormalColor(o);
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    return decIndent(o, indent);
  }
};

class Compare : public Expression {
public:
  RelationalOp op;
  WasmType inputType;
  Expression *left, *right;

  Compare() {
    type = WasmType::i32; // output is always i32
  }

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    prepareColor(o) << printWasmType(inputType) << '.';
    switch (op) {
      case Eq:  o << "eq"; break;
      case Ne:  o << "ne"; break;
      case LtS: o << "lt_s"; break;
      case LtU: o << "lt_u"; break;
      case LeS: o << "le_s"; break;
      case LeU: o << "le_u"; break;
      case GtS: o << "gt_s"; break;
      case GtU: o << "gt_u"; break;
      case GeS: o << "ge_s"; break;
      case GeU: o << "ge_u"; break;
      case Lt:  o << "lt"; break;
      case Le:  o << "le"; break;
      case Gt:  o << "gt"; break;
      case Ge:  o << "ge"; break;
      default: abort();
    }
    restoreNormalColor(o);
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    return decIndent(o, indent);
  }
};

class Convert : public Expression {
public:
  ConvertOp op;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    prepareColor(o);
    switch (op) {
      case ConvertUInt32: o << "f64.convert_u/i32"; break;
      case ConvertSInt32: o << "f64.convert_s/i32"; break;
      case TruncSFloat64: o << "i32.trunc_s/f64"; break;
      default: abort();
    }
    restoreNormalColor(o);
    incIndent(o, indent);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Host : public Expression {
public:
  HostOp op;
  ExpressionList operands;
};

// Globals

struct NameType {
  Name name;
  WasmType type;
  NameType() : name(nullptr), type(none) {}
  NameType(Name name, WasmType type) : name(name), type(type) {}
};

class Function {
public:
  Name name;
  WasmType result;
  std::vector<NameType> params;
  std::vector<NameType> locals;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "func ", true) << name;
    if (params.size() > 0) {
      for (auto& param : params) {
        o << ' ';
        printMinorOpening(o, "param ") << param.name << ' ' << printWasmType(param.type) << ")";
      }
    }
    if (result != none) {
      o << ' ';
      printMinorOpening(o, "result ") << printWasmType(result) << ")";
    }
    incIndent(o, indent);
    for (auto& local : locals) {
      doIndent(o, indent);
      printMinorOpening(o, "local ") << local.name << ' ' << printWasmType(local.type) << ")\n";
    }
    printFullLine(o, indent, body);
    return decIndent(o, indent);
  }
};

class Import {
public:
  Name name, module, base; // name = module.base
  FunctionType type;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "import ") << name << ' ';
    printText(o, module.str) << ' ';
    printText(o, base.str) << ' ';
    type.print(o, indent);
    return o << ')';
  }
};

class Export {
public:
  Name name;
  Name value;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "export ");
    return printText(o, name.str) << ' ' << value << ')';
  }
};

class Table {
public:
  std::vector<Name> names;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "table");
    for (auto name : names) {
      o << ' ' << name;
    }
    return o << ')';
  }
};

class Module {
public:
  // wasm contents
  std::map<Name, FunctionType*> functionTypes;
  std::map<Name, Import> imports;
  std::vector<Export> exports;
  Table table;
  std::vector<Function*> functions;

  Module() {}

  friend std::ostream& operator<<(std::ostream &o, Module module) {
    unsigned indent = 0;
    printOpening(o, "module", true);
    incIndent(o, indent);
    doIndent(o, indent);
    printOpening(o, "memory") << " 16777216)\n"; // XXX
    for (auto& curr : module.functionTypes) {
      doIndent(o, indent);
      curr.second->print(o, indent, true);
      o << '\n';
    }
#if 0
    for (auto& curr : module.imports) {
      doIndent(o, indent);
      curr.second.print(o, indent);
      o << '\n';
    }
#endif
    for (auto& curr : module.exports) {
      doIndent(o, indent);
      curr.print(o, indent);
      o << '\n';
    }
    if (module.table.names.size() > 0) {
      doIndent(o, indent);
      module.table.print(o, indent);
      o << '\n';
    }
    for (auto& curr : module.functions) {
      doIndent(o, indent);
      curr->print(o, indent);
      o << '\n';
    }
    decIndent(o, indent);
    return o << '\n';
  }
};

//
// Simple WebAssembly AST walker
//

struct WasmWalker {
  wasm::Arena* allocator; // use an existing allocator, or null if no allocations

  WasmWalker() : allocator(nullptr) {}
  WasmWalker(wasm::Arena* allocator) : allocator(allocator) {}

  // Each method receives an AST pointer, and it is replaced with what is returned.
  virtual Expression* walkBlock(Block *curr) { return curr; };
  virtual Expression* walkIf(If *curr) { return curr; };
  virtual Expression* walkLoop(Loop *curr) { return curr; };
  virtual Expression* walkLabel(Label *curr) { return curr; };
  virtual Expression* walkBreak(Break *curr) { return curr; };
  virtual Expression* walkSwitch(Switch *curr) { return curr; };
  virtual Expression* walkCall(Call *curr) { return curr; };
  virtual Expression* walkCallImport(CallImport *curr) { return curr; };
  virtual Expression* walkCallIndirect(CallIndirect *curr) { return curr; };
  virtual Expression* walkGetLocal(GetLocal *curr) { return curr; };
  virtual Expression* walkSetLocal(SetLocal *curr) { return curr; };
  virtual Expression* walkLoad(Load *curr) { return curr; };
  virtual Expression* walkStore(Store *curr) { return curr; };
  virtual Expression* walkConst(Const *curr) { return curr; };
  virtual Expression* walkUnary(Unary *curr) { return curr; };
  virtual Expression* walkBinary(Binary *curr) { return curr; };
  virtual Expression* walkCompare(Compare *curr) { return curr; };
  virtual Expression* walkConvert(Convert *curr) { return curr; };
  virtual Expression* walkHost(Host *curr) { return curr; };
  virtual Expression* walkNop(Nop *curr) { return curr; };

  // children-first
  Expression *walk(Expression *curr) {
    if (!curr) return curr;

    if (Block *cast = dynamic_cast<Block*>(curr)) {
      ExpressionList& list = cast->list;
      for (size_t z = 0; z < list.size(); z++) {
        list[z] = walk(list[z]);
      }
      return walkBlock(cast);
    }
    if (If *cast = dynamic_cast<If*>(curr)) {
      cast->condition = walk(cast->condition);
      cast->ifTrue = walk(cast->ifTrue);
      cast->ifFalse = walk(cast->ifFalse);
      return walkIf(cast);
    }
    if (Loop *cast = dynamic_cast<Loop*>(curr)) {
      cast->body = walk(cast->body);
      return walkLoop(cast);
    }
    if (Label *cast = dynamic_cast<Label*>(curr)) {
      return walkLabel(cast);
    }
    if (Break *cast = dynamic_cast<Break*>(curr)) {
      cast->condition = walk(cast->condition);
      cast->value = walk(cast->value);
      return walkBreak(cast);
    }
    if (Switch *cast = dynamic_cast<Switch*>(curr)) {
      cast->value = walk(cast->value);
      for (auto& curr : cast->cases) {
        curr.body = walk(curr.body);
      }
      cast->default_ = walk(cast->default_);
      return walkSwitch(cast);
    }
    if (Call *cast = dynamic_cast<Call*>(curr)) {
      ExpressionList& list = cast->operands;
      for (size_t z = 0; z < list.size(); z++) {
        list[z] = walk(list[z]);
      }
      return walkCall(cast);
    }
    if (CallImport *cast = dynamic_cast<CallImport*>(curr)) {
      ExpressionList& list = cast->operands;
      for (size_t z = 0; z < list.size(); z++) {
        list[z] = walk(list[z]);
      }
      return walkCallImport(cast);
    }
    if (CallIndirect *cast = dynamic_cast<CallIndirect*>(curr)) {
      cast->target = walk(cast->target);
      ExpressionList& list = cast->operands;
      for (size_t z = 0; z < list.size(); z++) {
        list[z] = walk(list[z]);
      }
      return walkCallIndirect(cast);
    }
    if (GetLocal *cast = dynamic_cast<GetLocal*>(curr)) {
      return walkGetLocal(cast);
    }
    if (SetLocal *cast = dynamic_cast<SetLocal*>(curr)) {
      cast->value = walk(cast->value);
      return walkSetLocal(cast);
    }
    if (Load *cast = dynamic_cast<Load*>(curr)) {
      cast->ptr = walk(cast->ptr);
      return walkLoad(cast);
    }
    if (Store *cast = dynamic_cast<Store*>(curr)) {
      cast->ptr = walk(cast->ptr);
      cast->value = walk(cast->value);
      return walkStore(cast);
    }
    if (Const *cast = dynamic_cast<Const*>(curr)) {
      return walkConst(cast);
    }
    if (Unary *cast = dynamic_cast<Unary*>(curr)) {
      cast->value = walk(cast->value);
      return walkUnary(cast);
    }
    if (Binary *cast = dynamic_cast<Binary*>(curr)) {
      cast->left = walk(cast->left);
      cast->right = walk(cast->right);
      return walkBinary(cast);
    }
    if (Compare *cast = dynamic_cast<Compare*>(curr)) {
      cast->left = walk(cast->left);
      cast->right = walk(cast->right);
      return walkCompare(cast);
    }
    if (Convert *cast = dynamic_cast<Convert*>(curr)) {
      cast->value = walk(cast->value);
      return walkConvert(cast);
    }
    if (Host *cast = dynamic_cast<Host*>(curr)) {
      ExpressionList& list = cast->operands;
      for (size_t z = 0; z < list.size(); z++) {
        list[z] = walk(list[z]);
      }
      return walkHost(cast);
    }
    if (Nop *cast = dynamic_cast<Nop*>(curr)) {
      return walkNop(cast);
    }
    abort();
  }

  void startWalk(Function *func) {
    func->body = walk(func->body);
  }
};

} // namespace wasm

#endif // __wasm_h__

