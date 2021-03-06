/* **************************************************************************
 * Expression code generation
 *
 * Generates byte code for expressions in standard-type functions.
 *
 * Part of GTRPE by Gren Drake
 * **************************************************************************/
#include <ostream>
#include <sstream>
#include "expression.h"
#include "gamedata.h"
#include "opcode.h"

void handle_asm_stmt(GameData &gamedata, FunctionDef *function, List *list);
void handle_call_stmt(GameData &gamedata, FunctionDef *function, List *list);
void handle_reserved_stmt(GameData &gamedata, FunctionDef *function, List *list);
void stmt_and(GameData &gamedata, FunctionDef *function, List *list);
void stmt_asm(GameData &gamedata, FunctionDef *function, List *list);
void stmt_break(GameData &gamedata, FunctionDef *function, List *list);
void stmt_continue(GameData &gamedata, FunctionDef *function, List *list);
void stmt_dec(GameData &gamedata, FunctionDef *function, List *list);
void stmt_do_while(GameData &gamedata, FunctionDef *function, List *list);
void stmt_if(GameData &gamedata, FunctionDef *function, List *list);
void stmt_inc(GameData &gamedata, FunctionDef *function, List *list);
void stmt_list(GameData &gamedata, FunctionDef *function, List *list);
void stmt_return(GameData &gamedata, FunctionDef *function, List *list);
void stmt_string(GameData &gamedata, FunctionDef *function, List *list);
void stmt_option(GameData &gamedata, FunctionDef *function, List *list);
void stmt_or(GameData &gamedata, FunctionDef *function, List *list);
void stmt_print(GameData &gamedata, FunctionDef *function, List *list);
void stmt_print_uf(GameData &gamedata, FunctionDef *function, List *list);
void stmt_proc(GameData &gamedata, FunctionDef *function, List *list);
void stmt_while(GameData &gamedata, FunctionDef *function, List *list);

void process_value(GameData &gamedata, FunctionDef *function, ListValue &value);

/* ************************************************************************** *
 * Reserved words and statements                                              *
 * ************************************************************************** */

StatementType statementTypes[] = {
    { "",           nullptr       },
    { "and",        stmt_and,       true    },
    { "asm",        stmt_asm,       false   },
    { "break",      stmt_break,     false   },
    { "continue",   stmt_continue,  false   },
    { "dec",        stmt_dec,       false   },
    { "do_while",   stmt_do_while,  false   },
    { "if",         stmt_if,        true    },
    { "inc",        stmt_inc,       false   },
    { "list",       stmt_list,      true    },
    { "return",     stmt_return,    false   },
    { "string",     stmt_string,    true    },
    { "option",     stmt_option,    false   },
    { "or",         stmt_or,        true    },
    { "print",      stmt_print,     false   },
    { "print_uf",   stmt_print_uf,  false   },
    { "proc",       stmt_proc,      true    },
    { "while",      stmt_while,     false   },
};

const StatementType& getReservedWord(const std::string &word) {
    for (const StatementType &stmt : statementTypes) {
        if (stmt.name == word) return stmt;
    }
    return statementTypes[0];
}

/* ************************************************************************** *
 * General list management functions                                          *
 * ************************************************************************** */

void dump_list(const List *list, std::ostream &out) {
    if (!list) return;
    out << "( ";
    for (const ListValue &v : list->values) {
        if (v.value.type == Value::Expression) {
            dump_list(v.list, out);
        } else {
            out << v.value;
        }
        out << ' ';
    }
    out << ')';
}

bool checkListSize(const List *list, int minSize, int maxSize) {
    int listSize = list->values.size();
    if (listSize >= minSize && listSize <= maxSize) {
        return true;
    }
    return false;
}


/* ************************************************************************** *
 * Handlers for statement types                                               *
 * ************************************************************************** */

void handle_asm_stmt(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values[0].value.type != Value::Opcode) {
        std::stringstream ss;
        ss << "Expected opcode, but found " << list->values[0].value.type << '.';
        gamedata.addError(list->values[0].origin, ErrorMsg::Error, ss.str());
        return;
    }

    const OpcodeDef *opcode = list->values[0].value.opcode;
    if (opcode->permissions & FORBID_EXPRESSION) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error, "Opcode " + opcode->name + " may not be used as an expression.");
        return;
    }
    unsigned wantedOpcodeCount = opcode->inputs + 1;

    if (wantedOpcodeCount < 255 && !checkListSize(list, wantedOpcodeCount, wantedOpcodeCount)) {
        std::stringstream ss;
        ss << "Opcode '" << opcode->name << "' expected " << opcode->inputs << " operands, but found ";
        ss << list->values.size() - 1 << '.';
        gamedata.addError(list->values[0].origin, ErrorMsg::Error, ss.str());
        return;
    }

    for (unsigned i = list->values.size() - 1; i >= 1; --i) {
        const ListValue &theValue = list->values[i];
        if (i == 1 && (list->values[0].value.opcode->code == OpcodeDef::Store ||
            list->values[0].value.opcode->code == OpcodeDef::GetOption)) {
            if (theValue.value.type == Value::None && list->values[0].value.opcode->code == OpcodeDef::GetOption) {
                function->addValue(theValue.origin, Value{Value::None});
            } else if (theValue.value.type != Value::LocalVar) {
                gamedata.addError(theValue.origin, ErrorMsg::Error,
                    "Store opcode must reference local variable.");
            } else {
                function->addValue(theValue.origin, Value{Value::VarRef, theValue.value.value});
            }
        } else {
            if (theValue.value.type == Value::Expression) {
                process_list(gamedata, function, theValue.list);
                if (gamedata.hasErrors()) return;
            } else {
                function->addValue(theValue.origin, theValue.value);
            }
        }
    }
    function->addOpcode(list->values[0].origin, list->values[0].value.opcode->code);
    if (list->values[0].value.opcode->outputs <= 0) {
        function->addValue(list->values[0].origin, Value{Value::None});
    }
}

void handle_call_stmt(GameData &gamedata, FunctionDef *function, List *list) {
    const ListValue &func = list->values[0];
    const int argumentCount = list->values.size() - 1;

    for (unsigned i = list->values.size() - 1; i >= 1; --i) {
        const ListValue &theValue = list->values[i];
        if (theValue.value.type == Value::Expression) {
            process_list(gamedata, function, theValue.list);
            if (gamedata.hasErrors()) return;
        } else {
            function->addValue(theValue.origin, theValue.value);
        }
    }

    function->addValue(func.origin, Value{Value::Integer, argumentCount});
    if (func.value.type == Value::Expression) {
        process_list(gamedata, function, func.list);
    } else {
        function->addValue(func.origin, func.value);
    }
    function->addOpcode(func.origin, OpcodeDef::Call);
}

void handle_reserved_stmt(GameData &gamedata, FunctionDef *function, List *list) {
    const std::string &word = list->values[0].value.text;
    for (const StatementType &stmt : statementTypes) {
        if (stmt.name == word) {
            stmt.handler(gamedata, function, list);
            if (!stmt.hasResult) {
                function->addOpcode(function->origin, OpcodeDef::PushNone);
            }
            return;
        }
    }

    std::stringstream ss;
    ss << word << " is not a valid expression command.";
    gamedata.addError(list->values[0].origin, ErrorMsg::Error, ss.str());
}


/* ************************************************************************** *
 * Handlers for reserved words                                                *
 * ************************************************************************** */

void stmt_and(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() < 3) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "and requires at least two arguments.");
        return;
    }

    const Origin &origin = list->values[0].origin;
    std::string after_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;
    std::string false_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;

    for (unsigned i = 1; i < list->values.size(); ++i) {
        process_value(gamedata, function, list->values[i]);
        function->addValue(origin, Value{Value::Symbol, 0, false_label});
        function->addOpcode(list->values[0].origin, OpcodeDef::JumpZero);
    }
    function->addValue(origin, Value{Value::Integer, 1});
    function->addValue(origin, Value{Value::Symbol, 0, after_label});
    function->addOpcode(list->values[0].origin, OpcodeDef::Jump);

    function->addLabel(origin, false_label);
    function->addValue(origin, Value{Value::Integer, 0});
    function->addLabel(origin, after_label);
}

void stmt_asm(GameData &gamedata, FunctionDef *function, List *list) {
    for (unsigned i = 1; i < list->values.size(); ++i) {
        const ListValue &lv = list->values[i];

        switch(lv.value.type) {
            case Value::Indirection:
                ++i;
                if (i >= list->values.size()) {
                    gamedata.addError(lv.origin, ErrorMsg::Error, "Indirection found at end of list.");
                } else {
                    ListValue &rlv = list->values[i];
                    if (rlv.value.type != Value::LocalVar) {
                        gamedata.addError(lv.origin, ErrorMsg::Error, "Indirection may only be used with local variables.");
                    } else {
                        rlv.value.type = Value::VarRef;
                        function->addValue(rlv.origin, rlv.value);
                    }
                }
                break;
            case Value::None:
            case Value::Integer:
            case Value::String:
            case Value::List:
            case Value::Map:
            case Value::Function:
            case Value::Object:
            case Value::Property:
            case Value::TypeId:
            case Value::LocalVar:
                function->addValue(lv.origin, lv.value);
                break;
            case Value::Opcode:
                if (lv.value.opcode->permissions & FORBID_ASM) {
                    std::stringstream ss;
                    ss << "Opcode " << lv.value.opcode->name;
                    ss << " may not be used explicitly.";
                    gamedata.addError(lv.origin, ErrorMsg::Error, ss.str());
                }
                function->addOpcode(lv.origin, lv.value.opcode->code);
                break;
            case Value::Symbol:
                if (i + 1 < list->values.size() && list->values[i + 1].value.type == Value::Colon) {
                    // define a local label
                    function->addLabel(lv.origin, lv.value.text);
                    ++i; // skip next symbol (the colon defining the label)
                } else {
                    // check if a local label and, if so, make jump target
                    function->addValue(lv.origin, Value{Value::Symbol, 0, lv.value.text});
                }
                break;
            case Value::Colon:
                if (i > 1) {
                    std::stringstream ss;
                    ss << "Value of type " << list->values[i-1].value.type << " is not a valid label.";
                    gamedata.addError(lv.origin, ErrorMsg::Error, ss.str());
                    break;
                }
            default: {
                std::stringstream ss;
                ss << "Unexpected value " << lv.value << " in asm code body.";
                gamedata.addError(lv.origin, ErrorMsg::Error, ss.str());
                }
        }
    }
}

void stmt_or(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() < 3) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "or requires at least two arguments.");
        return;
    }

    const Origin &origin = list->values[0].origin;
    std::string after_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;
    std::string true_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;

    for (unsigned i = 1; i < list->values.size(); ++i) {
        process_value(gamedata, function, list->values[i]);
        function->addValue(origin, Value{Value::Symbol, 0, true_label});
        function->addOpcode(list->values[0].origin, OpcodeDef::JumpNotZero);
    }
    function->addValue(origin, Value{Value::Integer, 0});
    function->addValue(origin, Value{Value::Symbol, 0, after_label});
    function->addOpcode(list->values[0].origin, OpcodeDef::Jump);

    function->addLabel(origin, true_label);
    function->addValue(origin, Value{Value::Integer, 1});
    function->addLabel(origin, after_label);
}

void stmt_break(GameData &gamedata, FunctionDef *function, List *list) {
    if (!checkListSize(list, 1, 1)) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "break statement cannot take arguments.");
        return;
    }

    if (function->breakLabels.empty()) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "break statement found outside loop.");
        return;
    }

    const Origin &origin = list->values[0].origin;
    function->addValue(origin, Value{Value::Symbol, 0, function->breakLabels.back()});
    function->addOpcode(origin, OpcodeDef::Jump);
}

void stmt_continue(GameData &gamedata, FunctionDef *function, List *list) {
    if (!checkListSize(list, 1, 1)) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "continue statement cannot take arguments.");
        return;
    }

    if (function->continueLabels.empty()) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "continue statement found outside loop.");
        return;
    }

    const Origin &origin = list->values[0].origin;
    function->addValue(origin, Value{Value::Symbol, 0, function->continueLabels.back()});
    function->addOpcode(origin, OpcodeDef::Jump);
}

void stmt_dec(GameData &gamedata, FunctionDef *function, List *list) {
    if (!checkListSize(list, 2, 3)) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "inc expression takes one or two arguments.");
        return;
    }

    if (list->values[1].value.type != Value::LocalVar) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "inc requires name of local variable.");
        return;
    }

    if (list->values.size() == 3) {
        process_value(gamedata, function, list->values[2]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::Integer, 1});
    }
    process_value(gamedata, function, list->values[1]);
    function->addOpcode(list->values[0].origin, OpcodeDef::Sub);
    function->addValue(list->values[1].origin, Value{Value::VarRef, list->values[1].value.value});
    function->addOpcode(list->values[0].origin, OpcodeDef::Store);
}

void stmt_do_while(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() != 3) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "Do-While statement must have two expressions.");
        return;
    }

    std::string start_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;
    std::string condition_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;
    std::string after_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;

    function->continueLabels.push_back(condition_label);
    function->breakLabels.push_back(after_label);

    const Origin &origin = list->values[0].origin;

    function->addLabel(origin, start_label);
    process_value(gamedata, function, list->values[1]);
    function->addOpcode(origin, OpcodeDef::StackPop);
    function->addLabel(origin, condition_label);
    process_value(gamedata, function, list->values[2]);
    function->addValue(origin, Value{Value::Symbol, 0, after_label});
    function->addOpcode(origin, OpcodeDef::JumpZero);
    function->addValue(origin, Value{Value::Symbol, 0, start_label});
    function->addOpcode(origin, OpcodeDef::Jump);
    function->addLabel(origin, after_label);

    function->continueLabels.pop_back();
    function->breakLabels.pop_back();
}

void stmt_if(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() < 3 || list->values.size() > 4) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "If expression must have two or three expressions.");
        return;
    }

    std::string after_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;
    std::string else_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;

    const Origin &origin = list->values[0].origin;
    process_value(gamedata, function, list->values[1]);
    function->addValue(origin, Value{Value::Symbol, 0, else_label});
    function->addOpcode(origin, OpcodeDef::JumpZero);
    process_value(gamedata, function, list->values[2]);
    function->addValue(origin, Value{Value::Symbol, 0, after_label});
    function->addOpcode(origin, OpcodeDef::Jump);
    function->addLabel(origin, else_label);
    if (list->values.size() >= 4) {
        process_value(gamedata, function, list->values[3]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::Integer, 0});
    }
    function->addLabel(origin, after_label);
}

void stmt_inc(GameData &gamedata, FunctionDef *function, List *list) {
    if (!checkListSize(list, 2, 3)) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "inc expression takes one or two arguments.");
        return;
    }

    if (list->values[1].value.type != Value::LocalVar) {
        gamedata.addError(list->values[1].origin, ErrorMsg::Error,
            "inc requires name of local variable.");
        return;
    }

    if (list->values.size() == 3) {
        process_value(gamedata, function, list->values[2]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::Integer, 1});
    }
    process_value(gamedata, function, list->values[1]);
    function->addOpcode(list->values[0].origin, OpcodeDef::Add);
    function->addValue(list->values[1].origin, Value{Value::VarRef, list->values[1].value.value});
    function->addOpcode(list->values[0].origin, OpcodeDef::Store);
}

void stmt_list(GameData &gamedata, FunctionDef *function, List *list) {
    function->addValue(list->values[0].origin, Value{Value::TypeId, static_cast<int>(Value::List)});
    function->addOpcode(list->values[0].origin, OpcodeDef::New);

    for (unsigned i = 1; i < list->values.size(); ++i) {
         function->addOpcode(list->values[0].origin, OpcodeDef::StackDup);
        process_value(gamedata, function, list->values[i]);
        function->addValue(list->values[0].origin, Value{Value::Integer, 0});
        function->addValue(list->values[0].origin, Value{Value::Integer, 1});
        function->addOpcode(list->values[0].origin, OpcodeDef::StackSwap);
        function->addOpcode(list->values[0].origin, OpcodeDef::ListPush);
    }
}

void stmt_return(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() > 2) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "May not return multiple values.");
        return;
    }

    if (list->values.size() > 1) {
        process_value(gamedata, function, list->values[1]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::None});
    }
    function->addOpcode(list->values[0].origin, OpcodeDef::Return);
}

void stmt_string(GameData &gamedata, FunctionDef *function, List *list) {
    function->addValue(list->values[0].origin, Value{Value::TypeId, static_cast<int>(Value::String)});
    function->addOpcode(list->values[0].origin, OpcodeDef::New);

    for (unsigned i = 1; i < list->values.size(); ++i) {
         function->addOpcode(list->values[0].origin, OpcodeDef::StackDup);
        process_value(gamedata, function, list->values[i]);
        function->addValue(list->values[0].origin, Value{Value::Integer, 0});
        function->addValue(list->values[0].origin, Value{Value::Integer, 1});
        function->addOpcode(list->values[0].origin, OpcodeDef::StackSwap);
        function->addOpcode(list->values[0].origin, OpcodeDef::StringAppend);
    }
}

void stmt_option(GameData &gamedata, FunctionDef *function, List *list) {
    if (!checkListSize(list, 2, 5)) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "option statement takes one to three arguments.");
        return;
    }

    process_value(gamedata, function, list->values[1]);
    if (list->values.size() >= 3) {
        process_value(gamedata, function, list->values[2]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::None});
    }
    if (list->values.size() >= 4) {
        process_value(gamedata, function, list->values[3]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::None});
    }
    if (list->values.size() >= 5) {
        process_value(gamedata, function, list->values[4]);
    } else {
        function->addValue(list->values[0].origin, Value{Value::None});
    }
    function->addOpcode(list->values[0].origin, OpcodeDef::AddOption);
}

void stmt_print(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() <= 1) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "print statement requires arguments.");
        return;
    }
    for (unsigned i = 1; i < list->values.size(); ++i) {
        process_value(gamedata, function, list->values[i]);
        function->addOpcode(list->values[0].origin, OpcodeDef::Say);
    }
}

void stmt_print_uf(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() <= 1) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "print_uf statement requires arguments.");
        return;
    }
    process_value(gamedata, function, list->values[1]);
    function->addOpcode(list->values[0].origin, OpcodeDef::SayUCFirst);

    for (unsigned i = 2; i < list->values.size(); ++i) {
        process_value(gamedata, function, list->values[i]);
        function->addOpcode(list->values[0].origin, OpcodeDef::Say);
    }
}

void stmt_proc(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() < 2) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "proc statement must contain at least one statement.");
        return;
    }

    for (unsigned i = 1; i < list->values.size(); ++i) {
        process_value(gamedata, function, list->values[i]);
        if (i != list->values.size() - 1) {
            function->addOpcode(function->origin, OpcodeDef::StackPop);
        }
    }
}

void stmt_while(GameData &gamedata, FunctionDef *function, List *list) {
    if (list->values.size() != 3) {
        gamedata.addError(list->values[0].origin, ErrorMsg::Error,
            "While statement must have two expressions.");
        return;
    }

    std::string start_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;
    std::string after_label = "__label_" + std::to_string(function->nextLabel);
    ++function->nextLabel;

    function->continueLabels.push_back(start_label);
    function->breakLabels.push_back(after_label);

    const Origin &origin = list->values[0].origin;

    function->addLabel(origin, start_label);
    process_value(gamedata, function, list->values[1]);
    function->addValue(origin, Value{Value::Symbol, 0, after_label});
    function->addOpcode(origin, OpcodeDef::JumpZero);
    process_value(gamedata, function, list->values[2]);
    function->addValue(origin, Value{Value::Symbol, 0, start_label});
    function->addOpcode(origin, OpcodeDef::Jump);
    function->addLabel(origin, after_label);

    function->continueLabels.pop_back();
    function->breakLabels.pop_back();
}


/* ************************************************************************** *
 * Core list processing function                                              *
 * ************************************************************************** */

void process_value(GameData &gamedata, FunctionDef *function, ListValue &value) {
    switch(value.value.type) {
        case Value::Reserved:
        case Value::Indirection:
        case Value::Opcode: {
            std::stringstream ss;
            ss << "Invalid expression value of type " << value.value.type << '.';
            gamedata.addError(value.origin, ErrorMsg::Error, ss.str());
            break; }
        case Value::Symbol: {
            std::stringstream ss;
            ss << "Undefined symbol " << value.value.text << '.';
            gamedata.addError(value.origin, ErrorMsg::Error, ss.str());
            break; }
        case Value::Expression:
            process_list(gamedata, function, value.list);
            if (gamedata.hasErrors()) return;
            break;
        default:
            function->addValue(value.origin, value.value);
    }
}

void process_list(GameData &gamedata, FunctionDef *function, List *list) {
    if (!list || list->values.empty()) return;

    switch (list->values[0].value.type) {
        case Value::Function:
        case Value::LocalVar:
        case Value::Expression:
            handle_call_stmt(gamedata, function, list);
            break;
        case Value::Opcode:
            handle_asm_stmt(gamedata, function, list);
            break;
        case Value::String:
            list->values.insert(list->values.begin(),
                    ListValue{list->values[0].origin,
                    {Value::Reserved, 0, "print"}});
            stmt_print(gamedata, function, list);
            function->addOpcode(function->origin, OpcodeDef::PushNone);
            break;
        case Value::Reserved:
            handle_reserved_stmt(gamedata, function, list);
            break;
        case Value::Symbol: {
            std::stringstream ss;
            ss << "Unrecognized name " << list->values[0].value.text << '.';
            gamedata.addError(list->values[0].origin, ErrorMsg::Error, ss.str());
            break; }
        default: {
            std::stringstream ss;
            ss << "Expression not permitted to begin with value of type ";
            ss << list->values[0].value.type << '.';
            gamedata.addError(list->values[0].origin, ErrorMsg::Error, ss.str());
            break; }
    }
}