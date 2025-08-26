#include "crtz.h"
#include "crtz_lang.hpp"
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <cctype>
#include <cstring>

using namespace std;

/*
  CRTZ language interpreter
  - Has classes, objects, methods (basic OOP)
  - Supports: class definitions, new instantiation, object.field access, object.method(args)
  - Features nodes, choices, set, if/else/goto, signals, expressions
  - Supports multi-line show syntax
  - Boolean type with match keyword and true/false literals
  - Includes a debugger
*/

// ----------------------- Lexer / Token -----------------------

enum TokenKind { TK_EOF, TK_IDENT, TK_NUMBER, TK_STRING, TK_SYM, TK_STRING_DEC, TK_TRUE, TK_FALSE,TK_PICTURE, TK_LOAD };

struct Token {
    TokenKind kind;
    string text;
    int number = 0;
    int line = 0;
    Token(TokenKind k = TK_EOF, string t = "", int l = 0) : kind(k), text(move(t)), line(l) {}
};

struct Lexer {
    string src;
    size_t i = 0;
    int line = 1;
    Lexer(const string& s) : src(s) {}

    char peek() { return i < src.size() ? src[i] : '\0'; }
    char get() {
        if (i < src.size()) {
            char c = src[i++];
            if (c == '\n') line++;
            return c;
        }
        return '\0';
    }
    void skipSpace() {
        while (isspace((unsigned char)peek())) {
            if (peek() == '\n') line++;
            i++;
        }
    }

    bool startsWith(const string& s) {
        return src.compare(i, s.size(), s) == 0;
    }

    Token next() {
        skipSpace();
        if (i >= src.size()) return Token(TK_EOF, "", line);

        char c = peek();

        if (startsWith("//")) {
            while (peek() && peek() != '\n') get();
            return next();
        }

        if (c == '"') {
            get();
            string out;
            while (peek()) {
                if (peek() == '\\' && i + 1 < src.size() && src[i + 1] == '"') {
                    get(); get(); // Skip \"
                    out.push_back('"');
                    continue;
                }
                if (peek() == '"') {
                    get();
                    break;
                }
                char ch = get();
                if (ch == '\\' && peek()) {
                    char esc = get();
                    if (esc == 'n') out.push_back('\n');
                    else out.push_back(esc);
                } else {
                    out.push_back(ch);
                }
            }
            return Token(TK_STRING, out, line);
        }

        if (isalpha((unsigned char)c) || c == '_') {
            string id;
            while (isalnum((unsigned char)peek()) || peek() == '_' || peek() == '.') id.push_back(get());
            if (id == "true") return Token(TK_TRUE, id, line);
            if (id == "false") return Token(TK_FALSE, id, line);
            if (id == "picture") return Token(TK_PICTURE, id, line);
            if (id == "load") return Token(TK_LOAD, id, line);
            return Token(TK_IDENT, id, line);
        }

        if (isdigit((unsigned char)c) || (c == '-' && i + 1 < src.size() && isdigit((unsigned char)src[i + 1]))) {
            string num;
            if (peek() == '-') num.push_back(get());
            while (isdigit((unsigned char)peek())) num.push_back(get());
            Token t(TK_NUMBER, num, line);
            try { t.number = stoi(num); }
            catch (...) { t.number = 0; }
            return t;
        }

        if (c == '\'') {
            get();
            string out;
            while (peek() && peek() != '\'') {
                char ch = get();
                if (ch == '\\' && peek()) {
                    char esc = get();
                    if (esc == 'n') out.push_back('\n');
                    else out.push_back(esc);
                } else {
                    out.push_back(ch);
                }
            }
            if (peek() == '\'') get();
            return Token(TK_STRING_DEC, out, line);
        }

        if (startsWith("<=") || startsWith(">=") || startsWith("==") || startsWith("!=") || startsWith("->")) {
            string s = src.substr(i, 2);
            i += 2;
            return Token(TK_SYM, s, line);
        }

        string s(1, get());
        return Token(TK_SYM, s, line);
    }
};

// ----------------------- Expression Engine -----------------------

int precedence(const string& op) {
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") return 1;
    if (op == "+" || op == "-") return 2;
    if (op == "*" || op == "/") return 3;
    return 0;
}

bool isOperator(const string& s) {
    static unordered_set<string> ops = { "+", "-", "*", "/", "==", "!=", "<", "<=", ">", ">=" };
    return ops.count(s);
}

vector<string> infixToRPN(const vector<string>& tokens) {
    vector<string> out;
    vector<string> st;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const string& t = tokens[i];
        if (t.empty()) continue;
        if (isOperator(t)) {
            while (!st.empty() && isOperator(st.back()) && precedence(st.back()) >= precedence(t)) {
                out.push_back(st.back()); st.pop_back();
            }
            st.push_back(t);
        } else if (t == "(") {
            st.push_back(t);
        } else if (t == ")") {
            while (!st.empty() && st.back() != "(") {
                out.push_back(st.back()); st.pop_back();
            }
            if (!st.empty() && st.back() == "(") st.pop_back();
        } else {
            out.push_back(t);
        }
    }
    while (!st.empty()) {
        out.push_back(st.back()); st.pop_back();
    }
    return out;
}

struct Program;

static pair<string, string> splitDot(const string& s) {
    size_t pos = s.find('.');
    if (pos == string::npos) return { s, "" };
    return { s.substr(0, pos), s.substr(pos + 1) };
}

int evalRPN(const vector<string>& rpn,
    unordered_map<string, int>& vars,
    unordered_map<string, bool>& boolVars,
    unordered_map<string, unordered_map<string, int>>& objects) {
    vector<long long> st;
    for (auto& t : rpn) {
        if (isOperator(t)) {
            if (st.size() < 2) return 0;
            long long b = st.back(); st.pop_back();
            long long a = st.back(); st.pop_back();
            long long r = 0;
            if (t == "+") r = a + b;
            else if (t == "-") r = a - b;
            else if (t == "*") r = a * b;
            else if (t == "/") r = b != 0 ? a / b : 0;
            else if (t == "==") r = (a == b);
            else if (t == "!=") r = (a != b);
            else if (t == "<") r = (a < b);
            else if (t == "<=") r = (a <= b);
            else if (t == ">") r = (a > b);
            else if (t == ">=") r = (a >= b);
            st.push_back(r);
        } else {
            if (!t.empty() && (isdigit((unsigned char)t[0]) || ((t[0] == '-' || t[0] == '+') && t.size() > 1 && isdigit((unsigned char)t[1])))) {
                st.push_back(stoll(t));
            } else if (t == "true") {
                st.push_back(1);
            } else if (t == "false") {
                st.push_back(0);
            } else {
                auto pr = splitDot(t);
                if (!pr.second.empty()) {
                    string inst = pr.first;
                    string field = pr.second;
                    if (objects.count(inst) && objects[inst].count(field)) {
                        st.push_back(objects[inst][field]);
                    } else {
                        st.push_back(0);
                    }
                } else {
                    if (boolVars.count(t)) {
                        st.push_back(boolVars[t] ? 1 : 0);
                    } else {
                        st.push_back(vars.count(t) ? vars[t] : 0);
                    }
                }
            }
        }
    }
    return st.empty() ? 0 : (int)st.back();
}

vector<string> tokenizeExpr(const string& s) {
    vector<string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (isspace((unsigned char)s[i])) { ++i; continue; }
        if (i + 1 < s.size()) {
            string two = s.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "==" || two == "!=") {
                out.push_back(two); i += 2; continue;
            }
        }
        char c = s[i];
        if (strchr("+-*/()<>", c)) {
            out.push_back(string(1, c));
            ++i; continue;
        }
        if (isdigit((unsigned char)c) || ((c == '-' || c == '+') && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))) {
            size_t j = i + 1;
            while (j < s.size() && isdigit((unsigned char)s[j])) j++;
            out.push_back(s.substr(i, j - i));
            i = j; continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i + 1;
            while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '.')) j++;
            out.push_back(s.substr(i, j - i));
            i = j; continue;
        }
        ++i;
    }
    return out;
}

// ----------------------- AST / OOP structures -----------------------

struct Choice { int id; string text; string target; };
struct Node {
    string name;
    string text;
    vector<Choice> choices;
    vector<string> actions;
    int definitionLine = 0;
};

struct ClassDef {
    string name;
    unordered_map<string, int> fields;
    unordered_map<string, vector<string>> methods;
    unordered_map<string, vector<string>> methodParams;
};

struct Room {
    string name;
    string description;
    unordered_map<string, string> exits;
    vector<string> items;
    vector<string> npcs;

    Room() = default;
    Room(const string& n, const string& desc) : name(n), description(desc) {}
};

struct Program {
    string npc;
    string desc;
    unordered_map<string, int> vars;
    unordered_map<string, bool> boolVars;
    unordered_map<string, string> stringVars;
    unordered_map<string, Node> nodes;
    string entry;

    unordered_map<string, ClassDef> classes;
    unordered_map<string, unordered_map<string, int>> objects;
    unordered_map<string, string> instanceClass;

    unordered_map<string, Room> rooms;
    string currentRoom;
};

// ----------------------- Debugger -----------------------

class Debugger {
public:
    void addBreakpoint(int line) {
        breakpoints.insert(line);
    }

    void removeBreakpoint(int line) {
        breakpoints.erase(line);
    }

    void step() {
        stepping = true;
    }

    void continueExecution() {
        stepping = false;
    }

    void check(int line, const Program& prog) {
        if (stepping || breakpoints.count(line)) {
            cout << "Breakpoint at line " << line << ". Type 'help' for commands." << endl;
            string command;
            while (true) {
                cout << "> ";
                getline(cin, command);
                if (command == "step" || command == "s") {
                    step();
                    break;
                } else if (command == "continue" || command == "c") {
                    continueExecution();
                    break;
                } else if (command.rfind("print", 0) == 0 || command.rfind("p", 0) == 0) {
                    string var;
                    size_t space_pos = command.find(' ');
                    if (space_pos != string::npos) {
                        var = command.substr(space_pos + 1);
                        printVar(var, prog);
                    } else {
                        cout << "Usage: print <variable>" << endl;
                    }
                } else if (command == "help" || command == "h") {
                    printHelp();
                } else if (command == "breakpoints" || command == "b") {
                    listBreakpoints();
                } else if (command.rfind("break", 0) == 0) {
                    string line_str;
                    size_t space_pos = command.find(' ');
                    if (space_pos != string::npos) {
                        line_str = command.substr(space_pos + 1);
                        try {
                            int line_num = stoi(line_str);
                            addBreakpoint(line_num);
                            cout << "Breakpoint added at line " << line_num << endl;
                        } catch (...) {
                            cout << "Invalid line number" << endl;
                        }
                    } else {
                        cout << "Usage: break <line>" << endl;
                    }
                } else if (command.rfind("delete", 0) == 0) {
                    string line_str;
                    size_t space_pos = command.find(' ');
                    if (space_pos != string::npos) {
                        line_str = command.substr(space_pos + 1);
                        try {
                            int line_num = stoi(line_str);
                            removeBreakpoint(line_num);
                            cout << "Breakpoint removed at line " << line_num << endl;
                        } catch (...) {
                            cout << "Invalid line number" << endl;
                        }
                    } else {
                        cout << "Usage: delete <line>" << endl;
                    }
                } else if (command == "variables" || command == "v") {
                    listVariables(prog);
                } else {
                    cout << "Unknown command. Type 'help' for available commands." << endl;
                }
            }
        }
    }

private:
    void printVar(const string& var, const Program& prog) {
        if (prog.vars.count(var)) {
            cout << var << " = " << prog.vars.at(var) << endl;
        } else if (prog.boolVars.count(var)) {
            cout << var << " = " << (prog.boolVars.at(var) ? "true" : "false") << endl;
        } else if (prog.stringVars.count(var)) {
            cout << var << " = " << prog.stringVars.at(var) << endl;
        } else {
            // Check if it's an object field
            auto pr = splitDot(var);
            if (!pr.second.empty() && prog.objects.count(pr.first) && prog.objects.at(pr.first).count(pr.second)) {
                cout << var << " = " << prog.objects.at(pr.first).at(pr.second) << endl;
            } else {
                cout << "Variable not found." << endl;
            }
        }
    }

    void listVariables(const Program& prog) {
        cout << "Integer variables:" << endl;
        for (const auto& var : prog.vars) {
            cout << "  " << var.first << " = " << var.second << endl;
        }
        
        cout << "Boolean variables:" << endl;
        for (const auto& var : prog.boolVars) {
            cout << "  " << var.first << " = " << (var.second ? "true" : "false") << endl;
        }
        
        cout << "String variables:" << endl;
        for (const auto& var : prog.stringVars) {
            cout << "  " << var.first << " = " << var.second << endl;
        }
        
        cout << "Object fields:" << endl;
        for (const auto& obj : prog.objects) {
            for (const auto& field : obj.second) {
                cout << "  " << obj.first << "." << field.first << " = " << field.second << endl;
            }
        }
    }

    void listBreakpoints() {
        if (breakpoints.empty()) {
            cout << "No breakpoints set." << endl;
        } else {
            cout << "Breakpoints at lines:";
            for (int line : breakpoints) {
                cout << " " << line;
            }
            cout << endl;
        }
    }

    void printHelp() {
        cout << "Debugger commands:" << endl;
        cout << "  step (s):           Execute the next line." << endl;
        cout << "  continue (c):       Continue execution until the next breakpoint." << endl;
        cout << "  print (p) <var>:    Print the value of a variable." << endl;
        cout << "  variables (v):      List all variables." << endl;
        cout << "  break (b) <line>:   Set a breakpoint at the specified line." << endl;
        cout << "  delete <line>:      Remove a breakpoint at the specified line." << endl;
        cout << "  breakpoints:        List all breakpoints." << endl;
        cout << "  help (h):           Show this help message." << endl;
    }

    unordered_set<int> breakpoints;
    bool stepping = false;
};

// ----------------------- Parser -----------------------

class Parser {
    Lexer lex;
    Token tk;
    Program prog;
public:
    Parser(const string& src) : lex(src) { tk = lex.next(); }
    Token peek() { return tk; }
    Token consume() {
        Token t = tk; tk = lex.next(); return t;
    }
    bool acceptIdent(const string& s) {
        if (tk.kind == TK_IDENT && tk.text == s) { consume(); return true; }
        return false;
    }
    bool expectSym(const string& s) {
        if (tk.kind == TK_SYM && tk.text == s) { consume(); return true; }
        cerr << "Error at line " << tk.line << ": Expected symbol '" << s << "' but got '" << tk.text << "'\n";
        return false;
    }
    void parse() {
        while (tk.kind != TK_EOF) {
            if (tk.kind == TK_IDENT || tk.kind == TK_PICTURE) {
                if (tk.text == "npc") { parseNpc(); }
                else if (tk.text == "desc") { parseDesc(); }
                else if (tk.text == "int" || tk.text == "string" || tk.text == "match") { parseVarDecl(); }
                else if (tk.text == "node") { parseNode(); }
                else if (tk.text == "class") { parseClass(); }
                else if (tk.text == "new") { parseNewInstance(); }
                else if (tk.text == "room") { parseRoom(); }
                else if (tk.kind == TK_PICTURE) { parsePicture(); }
                else {
                    cerr << "Error at line " << tk.line << ": Unknown top-level keyword: " << tk.text << "\n";
                    consume();
                }
            } else {
                consume();
            }
        }
    }
    void parseRoom() {
        consume();
        if (tk.kind != TK_IDENT) { cerr << "Error at line " << tk.line << ": room expects a name\n"; return; }
        string roomName = tk.text; consume();

        if (!(tk.kind == TK_SYM && tk.text == "{")) { cerr << "Error at line " << tk.line << ": expected '{' after room name\n"; return; }
        consume();

        Room room;
        room.name = roomName;

        while (!(tk.kind == TK_SYM && tk.text == "}") && tk.kind != TK_EOF) {
            if (tk.kind == TK_IDENT) {
                if (tk.text == "desc") {
                    consume();
                    if (tk.kind == TK_STRING) {
                        room.description = tk.text; consume();
                        expectSym(";");
                    }
                } else if (tk.text == "exit") {
                    consume();
                    if (tk.kind == TK_IDENT) {
                        string dir = tk.text; consume();
                        if (tk.kind == TK_IDENT) {
                            string target = tk.text; consume();
                            room.exits[dir] = target;
                            expectSym(";");
                        }
                    }
                } else if (tk.text == "item") {
                    consume();
                    if (tk.kind == TK_IDENT) {
                        room.items.push_back(tk.text); consume();
                        expectSym(";");
                    }
                } else if (tk.text == "npc") {
                    consume();
                    if (tk.kind == TK_IDENT) {
                        room.npcs.push_back(tk.text); consume();
                        expectSym(";");
                    }
                } else {
                    consume();
                }
            } else {
                consume();
            }
        }
        expectSym("}");
        prog.rooms[roomName] = room;
        if (prog.currentRoom.empty()) prog.currentRoom = roomName;
    }

    void parsePicture() {
    consume(); // Consume the "picture" keyword
    
    if (tk.kind != TK_IDENT) {
        cerr << "Error at line " << tk.line << ": picture expects an identifier\n";
        return;
    }
    
    string arrayName = tk.text;
    consume();
    
    // Parse array size
    if (!(tk.kind == TK_SYM && tk.text == "[")) {
        cerr << "Error at line " << tk.line << ": expected '[' after picture name\n";
        return;
    }
    consume();
    
    if (tk.kind != TK_NUMBER) {
        cerr << "Error at line " << tk.line << ": expected number for array size\n";
        return;
    }
    
    int arraySize = tk.number;
    consume();
    
    if (!(tk.kind == TK_SYM && tk.text == "]")) {
        cerr << "Error at line " << tk.line << ": expected ']' after array size\n";
        return;
    }
    consume();
    
    if (!(tk.kind == TK_SYM && tk.text == "=")) {
        cerr << "Error at line " << tk.line << ": expected '=' after array declaration\n";
        return;
    }
    consume();
    
    if (tk.kind != TK_LOAD) {
        cerr << "Error at line " << tk.line << ": expected 'load' keyword\n";
        return;
    }
    consume();
    
    if (!(tk.kind == TK_SYM && tk.text == "(")) {
        cerr << "Error at line " << tk.line << ": expected '(' after load\n";
        return;
    }
    consume();
    
    if (tk.kind != TK_STRING) {
        cerr << "Error at line " << tk.line << ": expected string for folder path\n";
        return;
    }
    
    string folderPath = tk.text;
    consume();
    
    if (!(tk.kind == TK_SYM && tk.text == ")")) {
        cerr << "Error at line " << tk.line << ": expected ')' after folder path\n";
        return;
    }
    consume();
    
    expectSym(";");
    
}

    void parseNpc() {
        consume();
        if (tk.kind == TK_STRING) {
            prog.npc = tk.text; consume();
            expectSym(";");
        } else { cerr << "Error at line " << tk.line << ": npc requires string\n"; }
    }
    void parseDesc() {
        consume();
        if (tk.kind == TK_STRING) {
            prog.desc = tk.text; consume();
            expectSym(";");
        } else { cerr << "Error at line " << tk.line << ": desc requires string\n"; }
    }

    void parseVarDecl() {
        string type = tk.text;
        consume();

        if (tk.kind == TK_IDENT) {
            string name = tk.text; consume();
            if (tk.kind == TK_SYM && tk.text == "=") {
                consume();
                if (type == "string") {
                    if (tk.kind == TK_STRING_DEC || tk.kind == TK_STRING) {
                        string value = tk.text; consume();
                        expectSym(";");
                        prog.stringVars[name] = value;
                    } else {
                        cerr << "Error at line " << tk.line << ": String variable requires string literal\n";
                    }
                } else if (type == "match") {
                    if (tk.kind == TK_TRUE || tk.kind == TK_FALSE) {
                        bool value = (tk.kind == TK_TRUE);
                        consume();
                        expectSym(";");
                        prog.boolVars[name] = value;
                    } else {
                        cerr << "Error at line " << tk.line << ": Boolean variable requires true or false\n";
                    }
                } else {
                    string expr;
                    while (!(tk.kind == TK_SYM && tk.text == ";") && tk.kind != TK_EOF) {
                        expr += tk.text;
                        consume();
                    }
                    expectSym(";");
                    auto tokens = tokenizeExpr(expr);
                    auto rpn = infixToRPN(tokens);
                    unordered_map<string, unordered_map<string, int>> emptyobjs;
                    int val = evalRPN(rpn, prog.vars, prog.boolVars, emptyobjs);
                    prog.vars[name] = val;
                }
            } else {
                if (type == "string") prog.stringVars[name] = "";
                else if (type == "match") prog.boolVars[name] = false;
                else prog.vars[name] = 0;
                expectSym(";");
            }
        } else {
            cerr << "Error at line " << tk.line << ": " << type << " expects identifier\n";
        }
    }

    void parseClass() {
        consume();
        if (tk.kind != TK_IDENT) { cerr << "Error at line " << tk.line << ": class expects a name\n"; return; }
        string className = tk.text; consume();
        if (!(tk.kind == TK_SYM && tk.text == "{")) { cerr << "Error at line " << tk.line << ": expected '{' after class name\n"; return; }
        consume();
        ClassDef cdef; cdef.name = className;

        while (!(tk.kind == TK_SYM && tk.text == "}") && tk.kind != TK_EOF) {
            if (tk.kind == TK_IDENT) {
                if (tk.text == "int") {
                    consume();
                    if (tk.kind == TK_IDENT) {
                        string fname = tk.text; consume();
                        int fval = 0;
                        if (tk.kind == TK_SYM && tk.text == "=") {
                            consume();
                            string expr;
                            while (!(tk.kind == TK_SYM && tk.text == ";") && tk.kind != TK_EOF) {
                                expr += tk.text; consume();
                            }
                            expectSym(";");
                            unordered_map<string, unordered_map<string, int>> emptyobjs;
                            auto tokens = tokenizeExpr(expr);
                            auto rpn = infixToRPN(tokens);
                            fval = evalRPN(rpn, prog.vars, prog.boolVars, emptyobjs);
                        } else {
                            expectSym(";");
                        }
                        cdef.fields[fname] = fval;
                    } else {
                        cerr << "Error at line " << tk.line << ": field expects identifier\n"; consume();
                    }
                } else if (tk.text == "void") {
                    consume();
                    if (tk.kind != TK_IDENT) { cerr << "Error at line " << tk.line << ": method expects a name\n"; continue; }
                    string mname = tk.text; consume();
                    vector<string> params;
                    if (!(tk.kind == TK_SYM && tk.text == "(")) { cerr << "Error at line " << tk.line << ": expected '(' after method name\n"; }
                    consume();
                    while (!(tk.kind == TK_SYM && tk.text == ")") && tk.kind != TK_EOF) {
                        if (tk.kind == TK_IDENT) {
                            string pname = tk.text;
                            params.push_back(pname);
                            consume();
                            if (tk.kind == TK_SYM && tk.text == ",") { consume(); continue; }
                        } else if (tk.kind == TK_SYM && tk.text == ",") { consume(); continue; }
                        else { consume(); }
                    }
                    expectSym(")");
                    if (!(tk.kind == TK_SYM && tk.text == "{")) { cerr << "Error at line " << tk.line << ": expected '{' for method body\n"; continue; }
                    consume();
                    vector<string> methodActions;
                    while (!(tk.kind == TK_SYM && tk.text == "}") && tk.kind != TK_EOF) {
                        string stmt;
                        while (!(tk.kind == TK_SYM && tk.text == ";") && !(tk.kind == TK_SYM && tk.text == "}") && tk.kind != TK_EOF) {
                            stmt += tk.text;
                            consume();
                        }
                        if (tk.kind == TK_SYM && tk.text == ";") {
                            string s = trim(stmt);
                            if (!s.empty()) methodActions.push_back(string("STMT ") + s);
                            consume();
                        } else {
                            string s = trim(stmt);
                            if (!s.empty()) methodActions.push_back(string("STMT ") + s);
                        }
                    }
                    expectSym("}");
                    cdef.methods[mname] = methodActions;
                    cdef.methodParams[mname] = params;
                } else {
                    cerr << "Error at line " << tk.line << ": Unknown class member: " << tk.text << "\n";
                    consume();
                }
            } else {
                consume();
            }
        }

        expectSym("}");
        prog.classes[className] = cdef;
    }

    void parseNewInstance() {
        consume();
        if (tk.kind != TK_IDENT) { cerr << "Error at line " << tk.line << ": new expects class name\n"; return; }
        string className = tk.text; consume();
        if (tk.kind != TK_IDENT) { cerr << "Error at line " << tk.line << ": new expects instance name\n"; return; }
        string instName = tk.text; consume();
        expectSym(";");
        if (!prog.classes.count(className)) {
            cerr << "Error at line " << tk.line << ": Unknown class " << className << " for new\n";
        } else {
            prog.objects[instName] = prog.classes[className].fields;
            prog.instanceClass[instName] = className;
        }
    }

    void parseNode() {
        int nodeLine = tk.line;
        consume();
        if (tk.kind == TK_IDENT) {
            string nodename = tk.text; consume();
            if (prog.entry.empty()) prog.entry = nodename;
            if (!(tk.kind == TK_SYM && tk.text == "{")) {
                cerr << "Error at line " << tk.line << ": expected '{' after node name\n";
                return;
            }
            consume();
            Node node; node.name = nodename; node.definitionLine = nodeLine;
            while (!(tk.kind == TK_SYM && tk.text == "}") && tk.kind != TK_EOF) {
                if (tk.kind == TK_IDENT) {
                    string kw = tk.text;
                    if (kw == "line") {
                        consume();
                        if (tk.kind == TK_STRING) { node.text = tk.text; consume(); }
                        expectSym(";");
                    } else if (kw == "show") {
                        consume();
                        vector<string> showTexts;
                        if (tk.kind == TK_STRING) {
                            showTexts.push_back(tk.text);
                            consume();
                            while (tk.kind == TK_SYM && tk.text == ",") {
                                consume();
                                if (tk.kind == TK_STRING) {
                                    showTexts.push_back(tk.text);
                                    consume();
                                } else {
                                    cerr << "Error at line " << tk.line << ": show expects string after comma\n";
                                    break;
                                }
                            }
                            expectSym(";");
                            for (const auto& text : showTexts) {
                                node.actions.push_back("SHOW " + text);
                            }
                        } else {
                            cerr << "Error at line " << tk.line << ": show requires string literal\n";
                        }
                    } else if (kw == "choice") {
                        consume();
                        if (tk.kind == TK_NUMBER) {
                            int id = tk.number; consume();
                            expectSym(":");
                            if (tk.kind == TK_STRING) {
                                string text = tk.text; consume();
                                if (tk.kind == TK_SYM && tk.text == "->") { consume(); }
                                else if (tk.kind == TK_SYM && tk.text == "-") {
                                    consume(); if (tk.kind == TK_SYM && tk.text == "") { consume(); }
                                }
                                if (tk.kind == TK_IDENT) {
                                    string target = tk.text; consume();
                                    expectSym(";");
                                    node.choices.push_back({ id, text, target });
                                } else {
                                    cerr << "Error at line " << tk.line << ": choice target expected\n";
                                }
                            } else {
                                cerr << "Error at line " << tk.line << ": choice text string expected\n";
                            }
                        } else {
                            cerr << "Error at line " << tk.line << ": choice id expected\n"; consume();
                        }
                    } else if (kw == "set") {
                        consume();
                        string name;
                        if (tk.kind == TK_IDENT) {
                            name = tk.text; consume();
                        } else { cerr << "Error at line " << tk.line << ": set expected identifier\n"; }
                        if (tk.kind == TK_SYM && tk.text == "=") {
                            consume();
                        } else {
                            cerr << "Error at line " << tk.line << ": expected '=' after set var\n";
                        }
                        string expr;
                        while (!(tk.kind == TK_SYM && tk.text == ";") && tk.kind != TK_EOF) {
                            expr += tk.text; consume();
                        }
                        expectSym(";");
                        node.actions.push_back("SET " + name + " " + expr);
                    } else if (kw == "signal") {
                        consume();
                        if (tk.kind == TK_IDENT) {
                            string name = tk.text; consume();
                            if (tk.kind == TK_SYM && tk.text == "=") consume();
                            string expr;
                            while (!(tk.kind == TK_SYM && tk.text == ";") && tk.kind != TK_EOF) {
                                expr += tk.text; consume();
                            }
                            expectSym(";");
                            node.actions.push_back("SIGNAL " + name + " " + expr);
                        } else {
                            cerr << "Error at line " << tk.line << ": signal name expected\n";
                        }
                    } else if (kw == "if") {
                        consume();
                        if (!(tk.kind == TK_SYM && tk.text == "(")) { cerr << "Error at line " << tk.line << ": if requires (\n"; }
                        else consume();
                        string cond;
                        while (!(tk.kind == TK_SYM && tk.text == ")") && tk.kind != TK_EOF) { cond += tk.text; consume(); }
                        expectSym(")");
                        if (tk.kind == TK_IDENT && tk.text == "goto") { consume(); }
                        else { cerr << "Error at line " << tk.line << ": if expects goto\n"; }
                        if (tk.kind == TK_IDENT) {
                            string target = tk.text; consume();
                            string elseTarget;
                            if (tk.kind == TK_IDENT && tk.text == "else") {
                                consume();
                                if (tk.kind == TK_IDENT && tk.text == "goto") { consume(); }
                                else { cerr << "Error at line " << tk.line << ": else expects goto\n"; }
                                if (tk.kind == TK_IDENT) {
                                    elseTarget = tk.text; consume();
                                } else { cerr << "Error at line " << tk.line << ": else goto target expected\n"; }
                            }
                            expectSym(";");
                            node.actions.push_back("IF " + cond + " GOTO " + target + (elseTarget.empty() ? "" : " ELSE " + elseTarget));
                        } else { cerr << "Error at line " << tk.line << ": goto target expected\n"; }
                    } else if (kw == "goto") {
                        consume();
                        if (tk.kind == TK_IDENT) {
                            string target = tk.text; consume();
                            expectSym(";");
                            node.actions.push_back("GOTO " + target);
                        } else { cerr << "Error at line " << tk.line << ": goto target expected\n"; }
                    } else if (kw == "end") {
                        consume(); expectSym(";"); node.actions.push_back("END");
                    } else {
                        string stmt;
                        while (!(tk.kind == TK_SYM && tk.text == ";") && tk.kind != TK_EOF) {
                            stmt += tk.text;
                            consume();
                        }
                        if (tk.kind == TK_SYM && tk.text == ";") { consume(); }
                        string s = trim(stmt);
                        if (!s.empty()) node.actions.push_back(string("STMT ") + s);
                    }
                } else {
                    consume();
                }
            }
            expectSym("}");
            prog.nodes[nodename] = node;
        } else {
            cerr << "Error at line " << tk.line << ": node expects name\n"; consume();
        }
    }

    Program& getProgram() { return prog; }

private:
    static string trim(const string& s) {
        size_t a = 0;
        while (a < s.size() && isspace((unsigned char)s[a])) ++a;
        size_t b = s.size();
        while (b > a && isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }
};

// ----------------------- Runtime helpers -----------------------

static int evalExpressionString(const string& expr,
    unordered_map<string, int>& vars,
    unordered_map<string, bool>& boolVars,
    unordered_map<string, unordered_map<string, int>>& objects) {
    auto tokens = tokenizeExpr(expr);
    auto rpn = infixToRPN(tokens);
    return evalRPN(rpn, vars, boolVars, objects);
}

static vector<string> splitArgs(const string& s) {
    vector<string> out;
    string cur;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') { depth++; cur.push_back(c); }
        else if (c == ')') { depth--; cur.push_back(c); }
        else if (c == ',' && depth == 0) {
            string t = cur;
            size_t a = 0; while (a < t.size() && isspace((unsigned char)t[a])) ++a;
            size_t b = t.size(); while (b > a && isspace((unsigned char)t[b - 1])) --b;
            out.push_back(t.substr(a, b - a));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        string t = cur;
        size_t a = 0; while (a < t.size() && isspace((unsigned char)t[a])) ++a;
        size_t b = t.size(); while (b > a && isspace((unsigned char)t[b - 1])) --b;
        out.push_back(t.substr(a, b - a));
    }
    vector<string> cleaned;
    for (auto& x : out) if (!x.empty()) cleaned.push_back(x);
    return cleaned;
}

static void executeMethod(Program& prog,
    const string& instanceName,
    const string& methodName,
    const vector<int>& argValues,
    const vector<string>& argNames,
    string& playerName);

static bool executeActionsWithContext(vector<string>& actions,
    Program& prog,
    unordered_map<string, int>& vars,
    unordered_map<string, bool>& boolVars,
    unordered_map<string, unordered_map<string, int>>& objects,
    const string& thisInstance,
    string& playerName,
    string& current_jump_target,
    bool& jumped) {
    for (auto& act : actions) {
        if (act.rfind("SET ", 0) == 0) {
            string rest = act.substr(4);
            size_t p = 0;
            string name;
            while (p < rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
            while (p < rest.size() && isspace((unsigned char)rest[p])) p++;
            string expr = rest.substr(p);

            auto pr = splitDot(name);
            if (!pr.second.empty()) {
                string inst = pr.first;
                string field = pr.second;
                int val = evalExpressionString(expr, vars, boolVars, objects);
                objects[inst][field] = val;
            } else {
                if (!thisInstance.empty() && objects.count(thisInstance) && objects[thisInstance].count(name)) {
                    int val = evalExpressionString(expr, vars, boolVars, objects);
                    objects[thisInstance][name] = val;
                } else if (boolVars.count(name)) {
                    int val = evalExpressionString(expr, vars, boolVars, objects);
                    boolVars[name] = (val != 0);
                } else {
                    int val = evalExpressionString(expr, vars, boolVars, objects);
                    vars[name] = val;
                }
            }
        } else if (act.rfind("SIGNAL ", 0) == 0) {
            string rest = act.substr(7);
            string name;
            size_t p = 0;
            while (p < rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
            while (p < rest.size() && isspace((unsigned char)rest[p])) p++;
            string expr = rest.substr(p);
            int val = evalExpressionString(expr, vars, boolVars, objects);
            cout << "[SIGNAL] " << name << " = " << val << "\n";
        } else if (act.rfind("IF ", 0) == 0) {
            size_t gpos = act.find(" GOTO ");
            if (gpos == string::npos) continue;
            string cond = act.substr(3, gpos - 3);
            string rest = act.substr(gpos + 6);
            size_t epos = rest.find(" ELSE ");
            string target = rest;
            string elseTarget;
            if (epos != string::npos) {
                target = rest.substr(0, epos);
                elseTarget = rest.substr(epos + 6);
            }
            int res = evalExpressionString(cond, vars, boolVars, objects);
            if (res) {
                current_jump_target = target;
                jumped = true;
                break;
            } else if (!elseTarget.empty()) {
                current_jump_target = elseTarget;
                jumped = true;
                break;
            }
        } else if (act.rfind("GOTO ", 0) == 0) {
            string target = act.substr(5);
            current_jump_target = target;
            jumped = true;
            break;
        } else if (act == "END") {
            cout << "[Dialogue ended]\n";
            return false;
        } else if (act.rfind("STMT ", 0) == 0) {
            string stmt = act.substr(5);
            string s = trim(stmt);
            size_t dotp = s.find('.');
            size_t paren = s.find('(');
            if (dotp != string::npos && paren != string::npos && paren > dotp) {
                string inst = s.substr(0, dotp);
                string method = s.substr(dotp + 1, paren - (dotp + 1));
                size_t rparen = s.rfind(')');
                string argsraw;
                if (rparen != string::npos && rparen > paren) argsraw = s.substr(paren + 1, rparen - paren - 1);
                else argsraw = s.substr(paren + 1);
                vector<string> argExprs = splitArgs(argsraw);
                vector<int> argVals;
                for (auto& ae : argExprs) {
                    int v = evalExpressionString(ae, vars, boolVars, objects);
                    argVals.push_back(v);
                }
                executeMethod(prog, inst, method, argVals, vector<string>{}, playerName);
            } else {
                if (s.rfind("print", 0) == 0) {
                    size_t p = s.find('(');
                    size_t q = s.rfind(')');
                    if (p != string::npos && q != string::npos && q > p) {
                        string inner = s.substr(p + 1, q - p - 1);
                        if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"') {
                            cout << inner.substr(1, inner.size() - 2) << "\n";
                        } else {
                            int val = evalExpressionString(inner, vars, boolVars, objects);
                            cout << (val ? "true" : "false") << "\n";
                        }
                    }
                }
            }
        } else if (act.rfind("SHOW ", 0) == 0) {
            string text = act.substr(5);
            size_t pos = 0;
            // Handle variable substitutions
            while ((pos = text.find("${", pos)) != string::npos) {
                size_t end = text.find("}", pos + 2);
                if (end == string::npos) break;

                string varName = text.substr(pos + 2, end - pos - 2);
                string value;

                if (prog.stringVars.count(varName)) {
                    value = prog.stringVars[varName];
                } else if (prog.boolVars.count(varName)) {
                    value = prog.boolVars[varName] ? "true" : "false";
                } else if (vars.count(varName)) {
                    value = to_string(vars[varName]);
                } else {
                    auto pr = splitDot(varName);
                    if (!pr.second.empty() && objects.count(pr.first) && objects[pr.first].count(pr.second)) {
                        value = to_string(objects[pr.first][pr.second]);
                    } else {
                        value = "0";
                    }
                }

                text.replace(pos, end - pos + 1, value);
                pos += value.length();
            }

            // Output the text
            cout << text << "\n";
        }
    }
    return true;
}

static void executeMethod(Program& prog,
    const string& instanceName,
    const string& methodName,
    const vector<int>& argValues,
    const vector<string>& argNames,
    string& playerName) {
    if (!prog.instanceClass.count(instanceName)) {
        cerr << "Runtime: unknown instance '" << instanceName << "'\n";
        return;
    }
    string cls = prog.instanceClass[instanceName];
    if (!prog.classes.count(cls)) {
        cerr << "Runtime: unknown class '" << cls << "' for instance '" << instanceName << "'\n";
        return;
    }
    ClassDef& cdef = prog.classes[cls];
    if (!cdef.methods.count(methodName)) {
        cerr << "Runtime: class '" << cls << "' has no method '" << methodName << "'\n";
        return;
    }
    unordered_map<string, int> localVars;
    unordered_map<string, bool> localBoolVars;
    for (auto& kv : prog.vars) localVars[kv.first] = kv.second;
    for (auto& kv : prog.boolVars) localBoolVars[kv.first] = kv.second;

    vector<string> paramNames;
    if (cdef.methodParams.count(methodName)) paramNames = cdef.methodParams[methodName];

    for (size_t i = 0; i < argValues.size() && i < paramNames.size(); ++i) {
        localVars[paramNames[i]] = argValues[i];
    }

    vector<string> actions = cdef.methods[methodName];

    unordered_map<string, unordered_map<string, int>> objects = prog.objects;
    if (!objects.count(instanceName)) objects[instanceName] = prog.classes[cls].fields;

    unordered_map<string, int> instanceFieldCopy = objects[instanceName];
    for (auto& kv : instanceFieldCopy) {
        if (!localVars.count(kv.first)) localVars[kv.first] = kv.second;
    }

    string jump_target;
    bool jumped = false;
    bool cont = executeActionsWithContext(actions, prog, localVars, localBoolVars, objects, instanceName, playerName, jump_target, jumped);
    for (auto& f : cdef.fields) {
        if (localVars.count(f.first)) {
            prog.objects[instanceName][f.first] = localVars[f.first];
        }
    }
    for (auto& kv : prog.vars) {
        if (localVars.count(kv.first)) {
            prog.vars[kv.first] = localVars[kv.first];
        }
    }
    for (auto& kv : prog.boolVars) {
        if (localBoolVars.count(kv.first)) {
            prog.boolVars[kv.first] = localBoolVars[kv.first];
        }
    }

    prog.objects = objects;
}

// ----------------------- Runtime / Runner -----------------------

void runProgram(Program& prog, string& playerName, Debugger& debugger) {
    string current = prog.entry;
    unordered_map<string, int> vars = prog.vars;
    unordered_map<string, bool> boolVars = prog.boolVars;
    unordered_map<string, unordered_map<string, int>>& objects = prog.objects;

    

    if (!prog.npc.empty()) {
        cout << "Npc: " << prog.npc << "\n";
    }
    if (!prog.desc.empty()) {
        cout << "Description: " << prog.desc << "\n\n";
    }

    while (true) {
        if (!prog.nodes.count(current)) {
            cerr << "Unknown node: " << current << "\n";
            break;
        }
        Node& node = prog.nodes[current];

        debugger.check(node.definitionLine, prog);

        if (!node.text.empty()) {
            string out = node.text;
            size_t pos = 0;
            while ((pos = out.find("[@You]", pos)) != string::npos) {
                out.replace(pos, 6, "[" + playerName + "]");
                pos += playerName.size() + 2;
            }
            size_t p = 0;
            while ((p = out.find("${", p)) != string::npos) {
                size_t q = out.find("}", p + 2);
                if (q == string::npos) break;
                string varname = out.substr(p + 2, q - (p + 2));
                string val;
                auto pr = splitDot(varname);
                if (!pr.second.empty()) {
                    if (objects.count(pr.first) && objects[pr.first].count(pr.second)) {
                        val = to_string(objects[pr.first][pr.second]);
                    } else {
                        val = "0";
                    }
                } else {
                    if (boolVars.count(varname)) {
                        val = boolVars[varname] ? "true" : "false";
                    } else if (vars.count(varname)) {
                        val = to_string(vars[varname]);
                    } else {
                        val = "0";
                    }
                }
                out.replace(p, q - p + 1, val);
                p += 1;
            }

            cout << out << "\n";
        }

        if (!node.choices.empty()) {
            for (auto& c : node.choices) {
                string text = c.text;
                size_t pos = 0;
                while ((pos = text.find("[@You]", pos)) != string::npos) {
                    text.replace(pos, 6, "[" + playerName + "]");
                    pos += playerName.size() + 2;
                }
                cout << "[" << c.id << "] " << text << "\n";
            }
            int sel = -1;
            while (true) {
                cout << "Choose: ";
                if (!(cin >> sel)) { cin.clear(); cin.ignore(1024, '\n'); cout << "Invalid\n"; continue; }
                bool ok = false;
                for (auto& c : node.choices) if (c.id == sel) { ok = true; current = c.target; break; }
                if (ok) break;
                cout << "Invalid choice\n";
            }
            continue;
        }

        bool jumped = false;
        string jump_target;
        for (auto& act : node.actions) {
            if (act.rfind("SET ", 0) == 0) {
                string rest = act.substr(4);
                string name;
                size_t p = 0;
                while (p < rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
                while (p < rest.size() && isspace((unsigned char)rest[p])) p++;
                string expr = rest.substr(p);
                auto pr = splitDot(name);
                if (!pr.second.empty()) {
                    string inst = pr.first;
                    string field = pr.second;
                    int val = evalExpressionString(expr, vars, boolVars, objects);
                    objects[inst][field] = val;
                } else {
                    if (boolVars.count(name)) {
                        int val = evalExpressionString(expr, vars, boolVars, objects);
                        boolVars[name] = (val != 0);
                    } else {
                        int val = evalExpressionString(expr, vars, boolVars, objects);
                        vars[name] = val;
                    }
                }
            } else if (act.rfind("SIGNAL ", 0) == 0) {
                string rest = act.substr(7);
                string name;
                size_t p = 0;
                while (p < rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
                while (p < rest.size() && isspace((unsigned char)rest[p])) p++;
                string expr = rest.substr(p);
                int val = evalExpressionString(expr, vars, boolVars, objects);
                cout << "[SIGNAL] " << name << " = " << (val ? "true" : "false") << "\n";
            } else if (act.rfind("IF ", 0) == 0) {
                size_t gpos = act.find(" GOTO ");
                if (gpos == string::npos) continue;
                string cond = act.substr(3, gpos - 3);
                string rest = act.substr(gpos + 6);
                size_t epos = rest.find(" ELSE ");
                string target = rest;
                string elseTarget;
                if (epos != string::npos) {
                    target = rest.substr(0, epos);
                    elseTarget = rest.substr(epos + 6);
                }
                int res = evalExpressionString(cond, vars, boolVars, objects);
                if (res) {
                    current = target;
                    jumped = true;
                    break;
                } else if (!elseTarget.empty()) {
                    current = elseTarget;
                    jumped = true;
                    break;
                }
            } else if (act.rfind("GOTO ", 0) == 0) {
                string target = act.substr(5);
                current = target;
                jumped = true;
                break;
            } else if (act == "END") {
                cout << "[Dialogue ended]\n";
                return;
            } else if (act.rfind("STMT ", 0) == 0) {
                string stmt = act.substr(5);
                string s = trim(stmt);
                size_t dotp = s.find('.');
                size_t paren = s.find('(');
                if (dotp != string::npos && paren != string::npos && paren > dotp) {
                    string inst = s.substr(0, dotp);
                    string method = s.substr(dotp + 1, paren - (dotp + 1));
                    size_t rparen = s.rfind(')');
                    string argsraw;
                    if (rparen != string::npos && rparen > paren) argsraw = s.substr(paren + 1, rparen - paren - 1);
                    else argsraw = s.substr(paren + 1);
                    vector<string> argExprs = splitArgs(argsraw);
                    vector<int> argVals;
                    for (auto& ae : argExprs) {
                        int v = evalExpressionString(ae, vars, boolVars, objects);
                        argVals.push_back(v);
                    }
                    executeMethod(prog, inst, method, argVals, vector<string>{}, playerName);
                } else {
                    if (s.rfind("new ", 0) == 0) {
                        string rest = s.substr(4);
                        istringstream iss(rest);
                        string className, instName;
                        iss >> className >> instName;
                        if (prog.classes.count(className)) {
                            prog.objects[instName] = prog.classes[className].fields;
                            prog.instanceClass[instName] = className;
                        } else {
                            cerr << "Unknown class in inline new: " << className << "\n";
                        }
                    } else if (s.rfind("print(", 0) == 0) {
                        size_t p = s.find('(');
                        size_t q = s.rfind(')');
                        if (p != string::npos && q != string::npos && q > p) {
                            string inner = s.substr(p + 1, q - p - 1);
                            if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"') {
                                cout << inner.substr(1, inner.size() - 2) << "\n";
                            } else {
                                int val = evalExpressionString(inner, vars, boolVars, objects);
                                cout << (val ? "true" : "false") << "\n";
                            }
                        }
                    }
                }
            } else if (act.rfind("SHOW ", 0) == 0) {
                string text = act.substr(5);
                size_t pos = 0;
                // Handle variable substitutions
                while ((pos = text.find("${", pos)) != string::npos) {
                    size_t end = text.find("}", pos + 2);
                    if (end == string::npos) break;

                    string varName = text.substr(pos + 2, end - pos - 2);
                    string value;

                    if (prog.stringVars.count(varName)) {
                        value = prog.stringVars[varName];
                    } else if (prog.boolVars.count(varName)) {
                        value = prog.boolVars[varName] ? "true" : "false";
                    } else if (vars.count(varName)) {
                        value = to_string(vars[varName]);
                    } else {
                        auto pr = splitDot(varName);
                        if (!pr.second.empty() && objects.count(pr.first) && objects[pr.first].count(pr.second)) {
                            value = to_string(objects[pr.first][pr.second]);
                        } else {
                            value = "0";
                        }
                    }

                    text.replace(pos, end - pos + 1, value);
                    pos += value.length();
                }

                // Output the text
                cout << text << "\n";
            }
        }

        if (jumped) continue;
        cout << "[End of Conversation]\n";
        return;
    }
}

// ----------------------- Library Wrapper APIs -----------------------

namespace CRTZ {

    void runSource(const std::string& source, const std::string& playerName, bool debug) {
        Parser parser(source);
        parser.parse();
        Program prog = parser.getProgram();
        string player = playerName;
        Debugger debugger;
        if (debug) {
            debugger.step();
        }
        runProgram(prog, player, debugger);
    }

    void runScript(const std::string& filename, const std::string& playerName, bool debug) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Could not open " << filename << std::endl;
            return;
        }

        std::string source((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        runSource(source, playerName, debug);
    }

} // namespace CRTZ

// ----------------------- main (CLI) -----------------------

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " [--debug] script.crtz\n";
        return 1;
    }

    bool debug = false;
    string filename;

    if (argc == 3 && string(argv[1]) == "--debug") {
        debug = true;
        filename = argv[2];
    } else {
        filename = argv[1];
    }

    ifstream in(filename);
    if (!in) { cerr << "Couldn't open file\n"; return 1; }
    string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

    Parser p(content);
    p.parse();
    Program prog = p.getProgram();
    string player = "Andrew";

    Debugger debugger;
    if (debug) {
        debugger.step();
    }

    runProgram(prog, player, debugger);

    return 0;
}