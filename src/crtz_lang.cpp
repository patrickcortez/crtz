// crtz_lang.cpp
#include "crtz.h"
#include "crtz_lang.hpp"
using namespace std;

/*
  Extended CRTZ language interpreter
  - Adds classes, objects, methods (basic OOP)
  - Supports: class definitions, new instantiation, object.field access, object.method(args)
  - Retains original features: nodes, choices, set, if/goto, signals, expressions
*/

// ----------------------- Lexer / Token -----------------------



enum TokenKind { TK_EOF, TK_IDENT, TK_NUMBER, TK_STRING, TK_SYM };

struct Token {
    TokenKind kind;
    string text;
    int number = 0;
    Token(TokenKind k=TK_EOF, string t=""): kind(k), text(move(t)) {}
};

struct Lexer {
    string src;
    size_t i = 0;
    Lexer(const string &s): src(s) {}

    char peek(){ return i < src.size() ? src[i] : '\0'; }
    char get(){ return i < src.size() ? src[i++] : '\0'; }
    void skipSpace(){
        while(isspace((unsigned char)peek())) i++;
    }

    bool startsWith(const string &s){
        return src.compare(i, s.size(), s) == 0;
    }

    Token next(){
        skipSpace();
        if(i >= src.size()) return Token(TK_EOF,"");

        char c = peek();

        // comments: // to endline
        if(startsWith("//")){
            while(peek() && peek() != '\n') get();
            return next();
        }

        // string literal "..."
        if(c == '"'){
            get(); // consume "
            string out;
            while(peek() && peek() != '"'){
                char ch = get();
                if(ch == '\\' && peek()){
                    char esc = get();
                    if(esc=='n') out.push_back('\n');
                    else out.push_back(esc);
                } else out.push_back(ch);
            }
            if(peek() == '"') get();
            return Token(TK_STRING, out);
        }

        // identifier or keyword (allow '.' inside identifiers for dotted names)
        if(isalpha((unsigned char)c) || c == '_'){
            string id;
            while(isalnum((unsigned char)peek()) || peek()=='_' || peek()=='.') id.push_back(get());
            return Token(TK_IDENT, id);
        }

        // number (supports negative numbers)
        if(isdigit((unsigned char)c) || (c=='-' && i+1 < src.size() && isdigit((unsigned char)src[i+1]))){
            string num;
            if(peek()=='-') num.push_back(get());
            while(isdigit((unsigned char)peek())) num.push_back(get());
            Token t(TK_NUMBER, num);
            try { t.number = stoi(num); } catch(...) { t.number = 0; }
            return t;
        }

        // symbols & two-char operators
        // handle: <=, >=, ==, !=, -> (arrow)
        if(startsWith("<=") || startsWith(">=") || startsWith("==") || startsWith("!=") || startsWith("->")){
            string s = src.substr(i,2);
            i += 2;
            return Token(TK_SYM, s);
        }

        // single-char symbols: ; : { } ( ) + - * / < > = , .
        string s(1, get());
        return Token(TK_SYM, s);
    }
};

// ----------------------- Expression Engine -----------------------

int precedence(const string &op){
    if(op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") return 1;
    if(op == "+" || op == "-") return 2;
    if(op == "*" || op == "/") return 3;
    return 0;
}

bool isOperator(const string &s){
    static unordered_set<string> ops = {"+" , "-", "*", "/", "==","!=","<","<=",">",">="};
    return ops.count(s);
}

vector<string> infixToRPN(const vector<string> &tokens){
    vector<string> out;
    vector<string> st;
    for(size_t i=0;i<tokens.size();++i){
        const string &t = tokens[i];
        if(t.empty()) continue;
        if(isOperator(t)){
            while(!st.empty() && isOperator(st.back()) && precedence(st.back()) >= precedence(t)){
                out.push_back(st.back()); st.pop_back();
            }
            st.push_back(t);
        } else if(t == "("){
            st.push_back(t);
        } else if(t == ")"){
            while(!st.empty() && st.back() != "("){
                out.push_back(st.back()); st.pop_back();
            }
            if(!st.empty() && st.back()=="(") st.pop_back();
        } else {
            // number or identifier
            out.push_back(t);
        }
    }
    while(!st.empty()){
        out.push_back(st.back()); st.pop_back();
    }
    return out;
}

// forward-declare Program type used here
struct Program;

// Helper to split dotted token "obj.field" into (obj, field)
static pair<string,string> splitDot(const string &s) {
    size_t pos = s.find('.');
    if(pos == string::npos) return {s, ""};
    return { s.substr(0,pos), s.substr(pos+1) };
}

// Evaluate RPN with access to global vars and objects (objects: instance -> (field->value))
int evalRPN(const vector<string> &rpn,
            unordered_map<string,int> &vars,
            unordered_map<string, unordered_map<string,int>> &objects) {
    vector<long long> st;
    for(auto &t : rpn){
        if(isOperator(t)){
            if(st.size() < 2) return 0;
            long long b = st.back(); st.pop_back();
            long long a = st.back(); st.pop_back();
            long long r = 0;
            if(t == "+") r = a + b;
            else if(t == "-") r = a - b;
            else if(t == "*") r = a * b;
            else if(t == "/") r = b != 0 ? a / b : 0;
            else if(t == "==") r = (a == b);
            else if(t == "!=") r = (a != b);
            else if(t == "<") r = (a < b);
            else if(t == "<=") r = (a <= b);
            else if(t == ">") r = (a > b);
            else if(t == ">=") r = (a >= b);
            st.push_back(r);
        } else {
            // number or var (support dotted names)
            if(!t.empty() && (isdigit((unsigned char)t[0]) || ((t[0]=='-'||t[0]=='+') && t.size()>1 && isdigit((unsigned char)t[1])))){
                st.push_back(stoll(t));
            } else {
                // dotted name?
                auto pr = splitDot(t);
                if(!pr.second.empty()){
                    string inst = pr.first;
                    string field = pr.second;
                    if(objects.count(inst) && objects[inst].count(field)){
                        st.push_back(objects[inst][field]);
                    } else {
                        // fallback to 0
                        st.push_back(0);
                    }
                } else {
                    // normal variable
                    st.push_back(vars.count(t) ? vars[t] : 0);
                }
            }
        }
    }
    return st.empty() ? 0 : (int)st.back();
}

// tokenize expression but treat dotted identifiers as single token
vector<string> tokenizeExpr(const string &s){
    vector<string> out;
    size_t i=0;
    while(i < s.size()){
        if(isspace((unsigned char)s[i])) { ++i; continue; }
        // two-char ops
        if(i+1 < s.size()){
            string two = s.substr(i,2);
            if(two=="<="||two==">="||two=="=="||two=="!="){
                out.push_back(two); i+=2; continue;
            }
        }
        char c = s[i];
        if(strchr("+-*/()<>", c)){
            out.push_back(string(1,c));
            ++i; continue;
        }
        // numbers or identifiers (identifiers may include dots)
        if(isdigit((unsigned char)c) || ((c=='-'||c=='+') && i+1<s.size() && isdigit((unsigned char)s[i+1]))){
            size_t j=i+1;
            while(j<s.size() && isdigit((unsigned char)s[j])) j++;
            out.push_back(s.substr(i, j-i));
            i=j; continue;
        }
        if(isalpha((unsigned char)c) || c=='_'){
            size_t j=i+1;
            while(j<s.size() && (isalnum((unsigned char)s[j])||s[j]=='_'||s[j]=='.')) j++;
            out.push_back(s.substr(i,j-i));
            i=j; continue;
        }
        // other char (ignore)
        ++i;
    }
    return out;
}

// ----------------------- AST / OOP structures -----------------------

struct Choice { int id; string text; string target; };
struct Node {
    string name;
    string line;
    vector<Choice> choices;
    vector<string> actions; // actions are strings like "SET name expr", "SIGNAL name expr", "IF ...", "GOTO X", "END", or "STMT <raw>"
};

struct ClassDef {
    string name;
    unordered_map<string,int> fields; // field default values
    unordered_map<string, vector<string>> methods; // methodName -> action strings
    unordered_map<string, vector<string>> methodParams; // methodName -> param names
};

struct Program {
    string npc;
    string desc;
    unordered_map<string,int> vars;
    unordered_map<string, Node> nodes;
    string entry; // first node seen

    // OOP
    unordered_map<string, ClassDef> classes; // className -> def
    unordered_map<string, unordered_map<string,int>> objects; // instanceName -> fields
    unordered_map<string,string> instanceClass; // instanceName -> className
};

// ----------------------- Parser -----------------------

class Parser {
    Lexer lex;
    Token tk;
    Program prog;
public:
    Parser(const string &src): lex(src) { tk = lex.next(); }
    Token peek(){ return tk; }
    Token consume(){
        Token t = tk; tk = lex.next(); return t;
    }
    bool acceptIdent(const string &s){
        if(tk.kind==TK_IDENT && tk.text==s){ consume(); return true; }
        return false;
    }
    bool expectSym(const string &s){
        if(tk.kind==TK_SYM && tk.text==s){ consume(); return true; }
        cerr << "Expected symbol '"<<s<<"' but got '"<<tk.text<<"'\n";
        return false;
    }

    void parse(){
        while(tk.kind != TK_EOF){
            if(tk.kind == TK_IDENT){
                if(tk.text == "npc"){ parseNpc(); }
                else if(tk.text == "desc"){ parseDesc(); }
                else if(tk.text == "int"){ parseVarDecl(); }
                else if(tk.text == "node"){ parseNode(); }
                else if(tk.text == "class"){ parseClass(); }
                else if(tk.text == "new"){ parseNewInstance(); }
                else { 
                    cerr << "Unknown top-level keyword: " << tk.text << "\n"; 
                    consume(); 
                }
            } else {
                // skip unexpected tokens
                consume();
            }
        }
    }

    void parseNpc(){
        consume(); // npc
        if(tk.kind==TK_STRING){
            prog.npc = tk.text; consume();
            expectSym(";");
        } else { cerr << "npc requires string\n"; }
    }
    void parseDesc(){
        consume(); // desc
        if(tk.kind==TK_STRING){
            prog.desc = tk.text; consume();
            expectSym(";");
        } else { cerr << "desc requires string\n"; }
    }

    void parseVarDecl(){
        consume(); // int
        if(tk.kind==TK_IDENT){
            string name = tk.text; consume();
            if(tk.kind==TK_SYM && tk.text=="="){
                consume();
                // gather expression tokens until ';'
                string expr;
                while(!(tk.kind==TK_SYM && tk.text==";") && tk.kind!=TK_EOF){
                    expr += tk.text;
                    consume();
                }
                expectSym(";");
                // evaluate using current program vars (objects empty at parse)
                auto tokens = tokenizeExpr(expr);
                auto rpn = infixToRPN(tokens);
                unordered_map<string, unordered_map<string,int>> emptyobjs;
                int val = evalRPN(rpn, prog.vars, emptyobjs);
                prog.vars[name] = val;
            } else {
                // int x; default 0
                prog.vars[name] = 0;
                expectSym(";");
            }
        } else {
            cerr << "int expects identifier\n";
        }
    }

    // Parse class definition:
    // class ClassName {
    //   int field = expr;
    //   void methodName(param1, param2) { ... }
    // }
    void parseClass(){
        consume(); // class
        if(tk.kind != TK_IDENT){ cerr<<"class expects a name\n"; return; }
        string className = tk.text; consume();
        if(!(tk.kind==TK_SYM && tk.text=="{")){ cerr<<"expected '{' after class name\n"; return; }
        consume(); // '{'
        ClassDef cdef; cdef.name = className;

        while(!(tk.kind==TK_SYM && tk.text=="}") && tk.kind != TK_EOF){
            if(tk.kind==TK_IDENT){
                if(tk.text == "int"){
                    // field declaration
                    consume(); // int
                    if(tk.kind==TK_IDENT){
                        string fname = tk.text; consume();
                        int fval = 0;
                        if(tk.kind==TK_SYM && tk.text=="="){
                            consume();
                            string expr;
                            while(!(tk.kind==TK_SYM && tk.text==";") && tk.kind!=TK_EOF){
                                expr += tk.text; consume();
                            }
                            expectSym(";");
                            unordered_map<string, unordered_map<string,int>> emptyobjs;
                            auto tokens = tokenizeExpr(expr);
                            auto rpn = infixToRPN(tokens);
                            fval = evalRPN(rpn, prog.vars, emptyobjs);
                        } else {
                            expectSym(";");
                        }
                        cdef.fields[fname] = fval;
                    } else {
                        cerr<<"field expects identifier\n"; consume();
                    }
                } else if(tk.text == "void"){
                    // method
                    consume(); // void
                    if(tk.kind != TK_IDENT){ cerr<<"method expects a name\n"; continue; }
                    string mname = tk.text; consume();
                    // params: (a, b)
                    vector<string> params;
                    if(!(tk.kind==TK_SYM && tk.text=="(")){ cerr<<"expected '(' after method name\n"; }
                    consume(); // '('
                    while(!(tk.kind==TK_SYM && tk.text==")") && tk.kind!=TK_EOF){
                        if(tk.kind==TK_IDENT){
                            // optionally type then name, or just name
                            string pname = tk.text; 
                            // allow "int x" or just "x" - if type present it will be 'int' token before name; here we are already at ident so accept as name
                            params.push_back(pname);
                            consume();
                            if(tk.kind==TK_SYM && tk.text==","){ consume(); continue; }
                        } else if(tk.kind==TK_SYM && tk.text==","){ consume(); continue; }
                        else { consume(); }
                    }
                    expectSym(")");
                    // method body { ... } capture statements until matching '}'
                    if(!(tk.kind==TK_SYM && tk.text=="{")){ cerr<<"expected '{' for method body\n"; continue; }
                    consume(); // '{'
                    vector<string> methodActions;
                    while(!(tk.kind==TK_SYM && tk.text=="}") && tk.kind!=TK_EOF){
                        // gather a statement until ';'
                        string stmt;
                        while(!(tk.kind==TK_SYM && tk.text==";") && !(tk.kind==TK_SYM && tk.text=="}") && tk.kind!=TK_EOF){
                            // include tokens with spacing
                            stmt += tk.text;
                            // if token is a string literal, wrap with quotes automatically (tk.text already without quotes for TK_STRING; but for consistency store as original)
                            if(tk.kind==TK_STRING){
                                // we stored string literal content already; to reconstruct add quotes
                                // but previous code when parsing nodes used TK_STRING directly; keep consistency: store string values without quotes but will treat substring properly later
                            }
                            consume();
                        }
                        if(tk.kind==TK_SYM && tk.text==";"){
                            // consumed up to ;
                            // normalize: trim whitespace
                            // store as STMT to be parsed at runtime
                            string s = trim(stmt);
                            if(!s.empty()) methodActions.push_back(string("STMT ") + s);
                            consume(); // consume ;
                        } else {
                            // maybe end '}' reached without ; (allow)
                            string s = trim(stmt);
                            if(!s.empty()) methodActions.push_back(string("STMT ") + s);
                        }
                    }
                    expectSym("}");
                    cdef.methods[mname] = methodActions;
                    cdef.methodParams[mname] = params;
                } else {
                    // unknown inside class: skip
                    cerr << "Unknown class member: " << tk.text << "\n";
                    consume();
                }
            } else {
                consume();
            }
        }

        expectSym("}");
        prog.classes[className] = cdef;
    }

    // parse 'new ClassName instanceName;'
    void parseNewInstance(){
        consume(); // new
        if(tk.kind != TK_IDENT){ cerr<<"new expects class name\n"; return; }
        string className = tk.text; consume();
        if(tk.kind != TK_IDENT){ cerr<<"new expects instance name\n"; return; }
        string instName = tk.text; consume();
        expectSym(";");
        if(!prog.classes.count(className)){
            cerr << "Unknown class " << className << " for new\n";
        } else {
            // create object instance copying default fields
            prog.objects[instName] = prog.classes[className].fields;
            prog.instanceClass[instName] = className;
        }
    }

    // parse node blocks as before but accept arbitrary statements and store as STMT
    void parseNode(){
        consume(); // node
        if(tk.kind==TK_IDENT){
            string nodename = tk.text; consume();
            if(prog.entry.empty()) prog.entry = nodename;
            if(!(tk.kind==TK_SYM && tk.text=="{")){
                cerr << "expected '{' after node name\n";
                return;
            }
            consume(); // '{'
            Node node; node.name = nodename;
            while(!(tk.kind==TK_SYM && tk.text=="}") && tk.kind!=TK_EOF){
                if(tk.kind==TK_IDENT){
                    string kw = tk.text;
                    if(kw == "line"){
                        consume();
                        if(tk.kind==TK_STRING){ node.line = tk.text; consume(); }
                        expectSym(";");
                    } else if(kw == "choice"){
                        consume();
                        // choice N : "text" -> TARGET;
                        if(tk.kind==TK_NUMBER){
                            int id = tk.number; consume();
                            expectSym(":");
                            if(tk.kind==TK_STRING){
                                string text = tk.text; consume();
                                if(tk.kind==TK_SYM && tk.text=="->"){ consume(); }
                                else if(tk.kind==TK_SYM && tk.text=="-"){ // maybe '-' then '>'
                                    consume(); if(tk.kind==TK_SYM && tk.text==">"){ consume(); } 
                                } else { /* arrow missing */ }
                                if(tk.kind==TK_IDENT){
                                    string target = tk.text; consume();
                                    expectSym(";");
                                    node.choices.push_back({id, text, target});
                                } else {
                                    cerr << "choice target expected\n";
                                }
                            } else {
                                cerr << "choice text string expected\n";
                            }
                        } else {
                            cerr << "choice id expected\n"; consume();
                        }
                    } else if(kw == "set"){
                        // set x = expr;   allow dotted LHS: hero.health
                        consume(); // set
                        // read LHS (may be dotted)
                        string name;
                        if(tk.kind==TK_IDENT){
                            name = tk.text; consume();
                        } else { cerr << "set expected identifier\n"; }
                        // if name is followed by '.' parts they were already read into token due to lexer treating dots inside id
                        if(tk.kind==TK_SYM && tk.text=="="){
                            consume();
                        } else if(tk.kind==TK_SYM && tk.text=="."){
                            // ideally should not happen because lexer combined dotted into single ident; but handle anyway
                            // collect dotted
                            while(tk.kind==TK_SYM && tk.text=="."){
                                consume();
                                if(tk.kind==TK_IDENT){ name += "." + tk.text; consume(); } else break;
                            }
                            if(tk.kind==TK_SYM && tk.text=="=") consume();
                            else cerr<<"expected '=' after set lhs\n";
                        } else {
                            cerr << "expected '=' after set var\n";
                        }
                        string expr;
                        while(!(tk.kind==TK_SYM && tk.text==";") && tk.kind!=TK_EOF){
                            expr += tk.text; consume();
                        }
                        expectSym(";");
                        // store as action string "SET <lhs> <expr>"
                        node.actions.push_back("SET " + name + " " + expr);
                    } else if(kw == "signal"){
                        consume(); // signal
                        if(tk.kind==TK_IDENT){
                            string name = tk.text; consume();
                            if(tk.kind==TK_SYM && tk.text=="=") consume();
                            string expr;
                            while(!(tk.kind==TK_SYM && tk.text==";") && tk.kind!=TK_EOF){
                                expr += tk.text; consume();
                            }
                            expectSym(";");
                            node.actions.push_back("SIGNAL " + name + " " + expr);
                        } else {
                            cerr << "signal name expected\n";
                        }
                    } else if(kw == "if"){
                        // if (cond) goto TARGET;
                        consume();
                        if(!(tk.kind==TK_SYM && tk.text=="(")){ cerr<<"if requires (\n"; }
                        else consume();
                        string cond;
                        while(!(tk.kind==TK_SYM && tk.text==")") && tk.kind!=TK_EOF){ cond += tk.text; consume(); }
                        expectSym(")");
                        if(tk.kind==TK_IDENT && tk.text=="goto"){ consume(); }
                        else { cerr <<"if expects goto\n"; }
                        if(tk.kind==TK_IDENT){
                            string target = tk.text; consume();
                            expectSym(";");
                            node.actions.push_back("IF " + cond + " GOTO " + target);
                        } else { cerr << "goto target expected\n"; }
                    } else if(kw == "goto"){
                        consume();
                        if(tk.kind==TK_IDENT){
                            string target = tk.text; consume();
                            expectSym(";");
                            node.actions.push_back("GOTO " + target);
                        } else { cerr << "goto target expected\n"; }
                    } else if(kw == "end"){
                        consume(); expectSym(";"); node.actions.push_back("END"); 
                    } else {
                        // Unknown keyword inside node — treat it as a raw statement to preserve extensibility
                        // Build statement until ';' and store as STMT
                        string stmt;
                        while(!(tk.kind==TK_SYM && tk.text==";") && tk.kind!=TK_EOF){
                            stmt += tk.text;
                            consume();
                        }
                        if(tk.kind==TK_SYM && tk.text==";") { consume(); }
                        string s = trim(stmt);
                        if(!s.empty()) node.actions.push_back(string("STMT ") + s);
                    }
                } else {
                    // skip other tokens
                    consume();
                }
            }
            expectSym("}");
            prog.nodes[nodename] = node;
        } else {
            cerr << "node expects name\n"; consume();
        }
    }

    Program& getProgram(){ return prog; }

private:
    // trim helper
    static string trim(const string &s) {
        size_t a = 0;
        while(a < s.size() && isspace((unsigned char)s[a])) ++a;
        size_t b = s.size();
        while(b > a && isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b-a);
    }
};

// ----------------------- Runtime helpers -----------------------

// Evaluate an expression string using current vars and objects
static int evalExpressionString(const string &expr,
            unordered_map<string,int> &vars,
            unordered_map<string, unordered_map<string,int>> &objects){
    auto tokens = tokenizeExpr(expr);
    auto rpn = infixToRPN(tokens);
    return evalRPN(rpn, vars, objects);
}

// parse argument list "a,b+1, 3" -> vector<string> of expressions
static vector<string> splitArgs(const string &s){
    vector<string> out;
    string cur;
    int depth = 0;
    for(size_t i=0;i<s.size();++i){
        char c = s[i];
        if(c == '(') { depth++; cur.push_back(c); }
        else if(c == ')') { depth--; cur.push_back(c); }
        else if(c==',' && depth==0){
            string t = cur;
            // trim
            size_t a=0; while(a<t.size() && isspace((unsigned char)t[a])) ++a;
            size_t b=t.size(); while(b>a && isspace((unsigned char)t[b-1])) --b;
            out.push_back(t.substr(a, b-a));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if(!cur.empty()){
        string t = cur;
        size_t a=0; while(a<t.size() && isspace((unsigned char)t[a])) ++a;
        size_t b=t.size(); while(b>a && isspace((unsigned char)t[b-1])) --b;
        out.push_back(t.substr(a, b-a));
    }
    // remove empty
    vector<string> cleaned;
    for(auto &x: out) if(!x.empty()) cleaned.push_back(x);
    return cleaned;
}

// Execute a method (class method) for a given instance
static void executeMethod(Program &prog,
                          const string &instanceName,
                          const string &methodName,
                          const vector<int> &argValues,
                          const vector<string> &argNames,
                          string &playerName);

// Execute a sequence of actions with context (vars + objects + thisInstance optional)
static bool executeActionsWithContext(vector<string> &actions,
                                      Program &prog,
                                      unordered_map<string,int> &vars,
                                      unordered_map<string, unordered_map<string,int>> &objects,
                                      const string &thisInstance,
                                      string &playerName) {
    // returns true if flow continues, false if ended
    bool jumped = false;
    string current_jump_target; // not used here for methods/nodes
    for(auto &act : actions){
        if(act.rfind("SET ",0)==0){
            // "SET name expr"
            string rest = act.substr(4);
            // name may be dotted
            size_t p = 0;
            string name;
            while(p < rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
            while(p < rest.size() && isspace((unsigned char)rest[p])) p++;
            string expr = rest.substr(p);

            // Evaluate expression using vars and objects
            int val = evalExpressionString(expr, vars, objects);

            // Assign to name: if dotted assign to object field, else if in method and thisInstance has field with that name then set object's field, else set global var
            auto pr = splitDot(name);
            if(!pr.second.empty()){
                string inst = pr.first;
                string field = pr.second;
                // ensure object exists
                if(objects.count(inst)){
                    objects[inst][field] = val;
                } else {
                    // fallback: create object? For now warn and create a new object with only this field
                    objects[inst][field] = val;
                }
            } else {
                // if thisInstance provided and field exists in that object's fields, treat as this.field assignment
                if(!thisInstance.empty() && objects.count(thisInstance) && objects[thisInstance].count(name)){
                    objects[thisInstance][name] = val;
                } else {
                    vars[name] = val;
                }
            }

        } else if(act.rfind("SIGNAL ",0)==0){
            string rest = act.substr(7);
            string name;
            size_t p=0;
            while(p<rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
            while(p<rest.size() && isspace((unsigned char)rest[p])) p++;
            string expr = rest.substr(p);
            int val = evalExpressionString(expr, vars, objects);
            // Default: print signal. In a real engine, send a callback/event
            cout << "[SIGNAL] " << name << " = " << val << "\n";
        } else if(act.rfind("IF ",0)==0){
            // "IF cond GOTO target"
            size_t gpos = act.find(" GOTO ");
            if(gpos == string::npos) continue;
            string cond = act.substr(3, gpos-3);
            string target = act.substr(gpos + 6);
            int res = evalExpressionString(cond, vars, objects);
            if(res){
                // we only support jumping in nodes (method jumps ignored)
                current_jump_target = target;
                jumped = true;
                break;
            }
        } else if(act.rfind("GOTO ",0)==0){
            string target = act.substr(5);
            current_jump_target = target;
            jumped = true;
            break;
        } else if(act == "END"){
            cout << "[Dialogue ended]\n";
            return false;
        } else if(act.rfind("STMT ",0)==0){
            string stmt = act.substr(5);
            // basic parsing for method calls: instance.method(args)
            // also allow plain function-like calls: method(args) which we treat as global function (not implemented)
            string s = trim(stmt);
            // handle method call pattern: <instance>.<method>(arg1, arg2)
            size_t dotp = s.find('.');
            size_t paren = s.find('(');
            if(dotp != string::npos && paren != string::npos && paren > dotp){
                string inst = s.substr(0, dotp);
                string method = s.substr(dotp+1, paren - (dotp+1));
                size_t rparen = s.rfind(')');
                string argsraw;
                if(rparen != string::npos && rparen > paren) argsraw = s.substr(paren+1, rparen - paren - 1);
                else argsraw = s.substr(paren+1);
                vector<string> argExprs = splitArgs(argsraw);
                vector<int> argVals;
                for(auto &ae : argExprs){
                    int v = evalExpressionString(ae, vars, objects);
                    argVals.push_back(v);
                }
                // call method on instance
                if(prog.classes.empty()) {
                    // nothing
                }
                // execute method using objects and vars context
                executeMethod(prog, inst, method, argVals, vector<string>{}, playerName);
            } else {
                // maybe a bare action like "return" or "print" or "set x = ..." (but set is handled above)
                // For now, do nothing or log unknown statement
                // simple support: allow "print(expr)" -> cout
                if(s.rfind("print",0)==0){
                    size_t p = s.find('(');
                    size_t q = s.rfind(')');
                    if(p != string::npos && q != string::npos && q>p){
                        string inner = s.substr(p+1, q-p-1);
                        // if inner is string literal "..." (note: in our tokenizer we stripped quotes for TK_STRING when building STMT)
                        if(inner.size()>=2 && inner.front()=='"' && inner.back()=='"'){
                            cout << inner.substr(1, inner.size()-2) << "\n";
                        } else {
                            int val = evalExpressionString(inner, vars, objects);
                            cout << val << "\n";
                        }
                    }
                } else {
                    // ignore unknown statement
                    // cout << "[STMT] " << s << "\n";
                }
            }
        } else {
            // unknown action
            // cout << "[Unknown action] " << act << "\n";
        }
    } // for action
    // if jumped true -> caller handles jump
    return true;
}

// execute a class method for a given instance
static void executeMethod(Program &prog,
                        const string &instanceName,
                        const string &methodName,
                        const vector<int> &argValues,
                        const vector<string> &argNames,
                        string &playerName) {
    // find object's class
    if(!prog.instanceClass.count(instanceName)){
        cerr << "Runtime: unknown instance '"<<instanceName<<"'\n";
        return;
    }
    string cls = prog.instanceClass[instanceName];
    if(!prog.classes.count(cls)){
        cerr << "Runtime: unknown class '"<<cls<<"' for instance '"<<instanceName<<"'\n";
        return;
    }
    ClassDef &cdef = prog.classes[cls];
    if(!cdef.methods.count(methodName)){
        cerr << "Runtime: class '"<<cls<<"' has no method '"<<methodName<<"'\n";
        return;
    }
    // bind parameters
    unordered_map<string,int> localVars; // local names including parameters and local temporaries
    // copy global vars as fallbacks so method can access globals via vars lookup if needed
    for(auto &kv : prog.vars) localVars[kv.first] = kv.second;

    // assign parameters by name if param names were given
    vector<string> paramNames;
    if(cdef.methodParams.count(methodName)) paramNames = cdef.methodParams[methodName];

    for(size_t i=0;i<argValues.size() && i<paramNames.size();++i){
        localVars[paramNames[i]] = argValues[i];
    }

    // The method actions have been stored as vector<string> (STMT ... or SET ...)
    vector<string> actions = cdef.methods[methodName];

    // execute actions: when resolving names inside actions:
    // - dotted names obj.field refer to objects map
    // - unqualified names: first check localVars, then check this instance fields, then globals
    // To allow evalRPN to see instance fields as e.g. when token 'health' appears, we will create a temporary objects map copy and a temporary vars map
    unordered_map<string, unordered_map<string,int>> objects = prog.objects; // copy (we will write back changes)
    // Make sure 'this' instance fields are accessible in objects map under instanceName
    if(!objects.count(instanceName)) objects[instanceName] = prog.classes[cls].fields;

    // local vars already filled, but evalRPN reads only vars and objects; we want plain unqualified identifiers in methods to resolve to thisInstance fields when present
    // So implement a small wrapper: when evalRPN sees token 'health' (no dot), it will check vars map; our localVars contains copies of global vars and params but not instance fields.
    // Trick: prefix this-instance fields into vars with a special naming or directly modify evalRPN. To keep changes minimal, we'll inject instance fields into vars with preference but remember to write back after method.
    // Copy instance fields into a map with names -> values but we must ensure param/local names override fields
    unordered_map<string,int> instanceFieldCopy = objects[instanceName];
    // Merge instance fields into localVars if not already present (so locals/params override fields)
    for(auto &kv : instanceFieldCopy){
        if(!localVars.count(kv.first)) localVars[kv.first] = kv.second;
    }

    // Execute actions with localVars and objects; but evalRPN will look into objects map for dotted names.
    // After execution, write back any changed instance fields from localVars to prog.objects
    bool cont = executeActionsWithContext(actions, prog, localVars, objects, instanceName, playerName);
    // write back instance fields: any field name from class fields that exists in localVars -> update prog.objects[instanceName]
    for(auto &f : cdef.fields){
        if(localVars.count(f.first)){
            prog.objects[instanceName][f.first] = localVars[f.first];
        }
    }
    // also update global variables (copy from localVars for keys that were originally global)
    for(auto &kv : prog.vars){
        if(localVars.count(kv.first)){
            prog.vars[kv.first] = localVars[kv.first];
        }
    }
    // also write back global vars that might have been changed inside method (conservative approach)
    for(auto &kv : localVars){
        if(!cdef.fields.count(kv.first) && !localVars.count(kv.first) /* redundant */) {
            // skip
        } else {
            // if key exists in global vars, update
            if(prog.vars.count(kv.first)) prog.vars[kv.first] = kv.second;
        }
    }

    // update prog.objects from objects copy (in case nested method call changed other instances)
    prog.objects = objects;
}

// ----------------------- Runtime / Runner -----------------------

void runProgram(Program &prog, string &playerName){
    string current = prog.entry;
    unordered_map<string,int> vars = prog.vars; // copy
    unordered_map<string, unordered_map<string,int>> &objects = prog.objects; // reference to actual objects map

    while(true){
        if(!prog.nodes.count(current)){
            cerr << "Unknown node: " << current << "\n";
            break;
        }
        Node &node = prog.nodes[current];

        // print node line with [@You] substitution
        if(!node.line.empty()){
            string out = node.line;
            size_t pos = 0;
            while((pos = out.find("[@You]", pos)) != string::npos){
                out.replace(pos, 6, "[" + playerName + "]");
                pos += playerName.size() + 2;
            }
            // variable interpolation `${var}` support (simple)
            // find ${ ... }
            size_t p = 0;
            while((p = out.find("${", p)) != string::npos){
                size_t q = out.find("}", p+2);
                if(q==string::npos) break;
                string varname = out.substr(p+2, q-(p+2));
                int val = 0;
                // dotted?
                auto pr = splitDot(varname);
                if(!pr.second.empty()){
                    if(objects.count(pr.first) && objects[pr.first].count(pr.second)) val = objects[pr.first][pr.second];
                } else {
                    if(vars.count(varname)) val = vars[varname];
                }
                out.replace(p, q-p+1, to_string(val));
                p += 1;
            }

            cout << out << "\n";
        }

        // show choices if any
        if(!node.choices.empty()){
            for(auto &c : node.choices){
                string text = c.text;
                size_t pos=0;
                while((pos = text.find("[@You]", pos)) != string::npos){
                    text.replace(pos, 6, "[" + playerName + "]");
                    pos += playerName.size() + 2;
                }
                cout << "[" << c.id << "] " << text << "\n";
            }
            int sel = -1;
            while(true){
                cout << "Choose: ";
                if(!(cin >> sel)){ cin.clear(); cin.ignore(1024,'\n'); cout<<"Invalid\n"; continue;}
                bool ok=false;
                for(auto &c : node.choices) if(c.id==sel){ ok=true; current = c.target; break;}
                if(ok) break;
                cout<<"Invalid choice\n";
            }
            continue; // jump to chosen node immediately
        }

        // execute actions in order
        bool jumped = false;
        for(auto &act : node.actions){
            if(act.rfind("SET ",0)==0){
                // "SET name expr"
                string rest = act.substr(4);
                string name;
                size_t p = 0;
                while(p < rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
                while(p < rest.size() && isspace((unsigned char)rest[p])) p++;
                string expr = rest.substr(p);
                int val = evalExpressionString(expr, vars, objects);
                auto pr = splitDot(name);
                if(!pr.second.empty()){
                    string inst = pr.first;
                    string field = pr.second;
                    objects[inst][field] = val;
                } else {
                    // if name exists as object field on any object? No: check if this variable corresponds to 'global' var or maybe a field of a known instance
                    vars[name] = val;
                }
            } else if(act.rfind("SIGNAL ",0)==0){
                string rest = act.substr(7);
                string name;
                size_t p=0;
                while(p<rest.size() && !isspace((unsigned char)rest[p])) name.push_back(rest[p++]);
                while(p<rest.size() && isspace((unsigned char)rest[p])) p++;
                string expr = rest.substr(p);
                int val = evalExpressionString(expr, vars, objects);
                cout << "[SIGNAL] " << name << " = " << val << "\n";
            } else if(act.rfind("IF ",0)==0){
                size_t gpos = act.find(" GOTO ");
                if(gpos == string::npos) continue;
                string cond = act.substr(3, gpos-3);
                string target = act.substr(gpos + 6);
                int res = evalExpressionString(cond, vars, objects);
                if(res){
                    current = target;
                    jumped = true;
                    break;
                }
            } else if(act.rfind("GOTO ",0)==0){
                string target = act.substr(5);
                current = target;
                jumped = true;
                break;
            } else if(act == "END"){
                cout << "[Dialogue ended]\n";
                return;
            } else if(act.rfind("STMT ",0)==0){
                string stmt = act.substr(5);
                string s = trim(stmt);
                // method call on instance: inst.method(args)
                size_t dotp = s.find('.');
                size_t paren = s.find('(');
                if(dotp != string::npos && paren != string::npos && paren > dotp){
                    string inst = s.substr(0, dotp);
                    string method = s.substr(dotp+1, paren - (dotp+1));
                    size_t rparen = s.rfind(')');
                    string argsraw;
                    if(rparen != string::npos && rparen > paren) argsraw = s.substr(paren+1, rparen - paren - 1);
                    else argsraw = s.substr(paren+1);
                    vector<string> argExprs = splitArgs(argsraw);
                    vector<int> argVals;
                    for(auto &ae : argExprs){
                        int v = evalExpressionString(ae, vars, objects);
                        argVals.push_back(v);
                    }
                    // call method on instance
                    executeMethod(prog, inst, method, argVals, vector<string>{}, playerName);
                } else {
                    // support new statement inline: new ClassName instanceName;
                    if(s.rfind("new ", 0) == 0){
                        // parse
                        string rest = s.substr(4);
                        // split by whitespace
                        istringstream iss(rest);
                        string className, instName;
                        iss >> className >> instName;
                        if(prog.classes.count(className)){
                            prog.objects[instName] = prog.classes[className].fields;
                            prog.instanceClass[instName] = className;
                        } else {
                            cerr << "Unknown class in inline new: " << className << "\n";
                        }
                    } else if(s.rfind("print(",0)==0){
                        size_t p = s.find('(');
                        size_t q = s.rfind(')');
                        if(p!=string::npos && q!=string::npos && q>p){
                            string inner = s.substr(p+1, q-p-1);
                            if(inner.size()>=2 && inner.front()=='"' && inner.back()=='"'){
                                cout << inner.substr(1, inner.size()-2) << "\n";
                            } else {
                                int v = evalExpressionString(inner, vars, objects);
                                cout << v << "\n";
                            }
                        }
                    } else {
                        // unknown statement: ignore or log
                        // cerr << "[Unknown statement] " << s << "\n";
                    }
                }
            } else {
                // unknown action
                // cerr << "[Unknown action] " << act << "\n";
            }
        } // for actions

        if(jumped) continue;
        // if no choices and no jump, end
        cout << "[End of node, no outgoing edges]\n";
        return;
    }
}

// ----------------------- Library Wrapper APIs -----------------------

namespace CRTZ {

void runSource(const std::string& source, const std::string& playerName) {
    Parser parser(source);
    parser.parse();
    Program prog = parser.getProgram();
    string player = playerName;
    runProgram(prog, player);
}

void runScript(const std::string& filename, const std::string& playerName) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Could not open " << filename << std::endl;
        return;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

    runSource(source, playerName);
}

} // namespace CRTZ

// ----------------------- main (CLI) -----------------------

int main(int argc, char** argv){
    if(argc < 2){
        cout << "Usage: " << argv[0] << " script.crtz\n";
        return 1;
    }
    // load file
    ifstream in(argv[1]);
    if(!in){ cerr << "Couldn't open file\n"; return 1; }
    string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    Parser p(content);
    p.parse();
    Program prog = p.getProgram();
    string player = "Andrew";
    cout << "NPC: " << prog.npc << "\n";
    cout << "DESC: " << prog.desc << "\n";

    runProgram(prog, player);

    return 0;
}
