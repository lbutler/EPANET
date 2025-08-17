#define _CRT_SECURE_NO_DEPRECATE
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mathexpr.h"

#define MAX_STACK_SIZE  1024

struct TreeNode
{
    int    opcode;
    int    ivar;
    double fvalue;
    struct TreeNode *left;
    struct TreeNode *right;
};

typedef struct TreeNode ExprTree;

// Forward declarations
static ExprTree* getTree(void);

static int    Err;
static int    Bc;
static int    PrevLex, CurLex;
static int    Len, Pos;
static char   *S;
static char   Token[255];
static int    Ivar;
static double Fvalue;

static int    (*getVariableIndex) (char *);

static int sametext(char *s1, char *s2)
{
    int i;
    for (i=0; toupper(s1[i]) == toupper(s2[i]); i++)
        if (!s1[i+1] && !s2[i+1]) return 1;
    return 0;
}

static int isDigit(char c)
{
    if (c >= '1' && c <= '9') return 1;
    if (c == '0') return 1;
    return 0;
}

static int isLetter(char c)
{
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '_') return 1;
    return 0;
}

static void getToken()
{
    char c[] = " ";
    Token[0] = '\0';
    while ( Pos <= Len && ( isLetter(S[Pos]) || isDigit(S[Pos]) ) )
    {
        c[0] = S[Pos];
        strcat(Token, c);
        Pos++;
    }
    Pos--;
}

static int getMathFunc()
{
    static char *MathFunc[] =  {"COS","SIN","TAN","COT","ABS","SGN",
        "SQRT","LOG","EXP","ASIN","ACOS","ATAN",
        "ACOT","SINH","COSH","TANH","COTH","LOG10",
        "STEP", NULL};
    int i = 0;
    while (MathFunc[i] != NULL)
    {
        if (sametext(MathFunc[i], Token)) return i+10;
        i++;
    }
    return 0;
}

static int getVariable()
{
    if (!getVariableIndex) return 0;
    Ivar = getVariableIndex(Token);
    if (Ivar >= 0) return 8;
    return 0;
}

static double getNumber()
{
    char c[] = " ";
    char sNumber[255];
    int  errflag = 0;
    int  decimalCount = 0;

    sNumber[0] = '\0';
    while (Pos < Len && isDigit(S[Pos]))
    {
        c[0] = S[Pos];
        strcat(sNumber, c);
        Pos++;
    }

    if (Pos < Len)
    {
        if (S[Pos] == '.')
        {
            decimalCount++;
            if (decimalCount > 1) Err = 1;
            strcat(sNumber, ".");
            Pos++;
            while (Pos < Len && isDigit(S[Pos]))
            {
                c[0] = S[Pos];
                strcat(sNumber, c);
                Pos++;
            }
        }
        if (Pos < Len && (S[Pos] == 'e' || S[Pos] == 'E'))
        {
            strcat(sNumber, "E");
            Pos++;
            if (Pos >= Len) errflag = 1;
            else
            {
                if (S[Pos] == '-' || S[Pos] == '+')
                {
                    c[0] = S[Pos];
                    strcat(sNumber, c);
                    Pos++;
                }
                if (Pos >= Len || !isDigit(S[Pos])) errflag = 1;
                else while ( Pos < Len && isDigit(S[Pos]))
                {
                    c[0] = S[Pos];
                    strcat(sNumber, c);
                    Pos++;
                }
            }
        }
    }
    Pos--;
    if (errflag) return 0;
    else return atof(sNumber);
}

static int getOperand()
{
    int code;
    switch(S[Pos])
    {
        case '(': code = 1;  break;
        case ')': code = 2;  break;
        case '+': code = 3;  break;
        case '-': code = 4;
            if (Pos < Len-1 && isDigit(S[Pos+1]) && (CurLex <= 6 || CurLex == 31))
            {
                Pos++;
                Fvalue = -getNumber();
                code = 7;
            }
            break;
        case '*': code = 5;  break;
        case '/': code = 6;  break;
        case '^': code = 31; break;
        default:  code = 0;
    }
    return code;
}

static int getLex()
{
    int n;
    while ( Pos < Len && S[Pos] == ' ' ) Pos++;
    if ( Pos >= Len ) return 0;
    n = getOperand();
    if ( n == 0 )
    {
        if ( isLetter(S[Pos]) )
        {
            getToken();
            n = getMathFunc();
            if ( n == 0 ) n = getVariable();
        }
        else if ( S[Pos] == '.' || isDigit(S[Pos]) )
        {
            n = 7;
            Fvalue = getNumber();
        }
    }
    Pos++;
    PrevLex = CurLex;
    CurLex = n;
    return n;
}

static ExprTree * newNode()
{
    ExprTree *node = (ExprTree *) malloc(sizeof(ExprTree));
    if (!node) Err = 2;
    else
    {
        node->opcode = 0;
        node->ivar   = -1;
        node->fvalue = 0.;
        node->left   = NULL;
        node->right  = NULL;
    }
    return node;
}

static ExprTree * getSingleOp(int *lex)
{
    int opcode;
    ExprTree *left;
    ExprTree *node;

    if ( *lex == 1 )
    {
        Bc++;
        left = getTree();
    }
    else
    {
        if ( *lex < 7 || *lex == 9 || *lex > 30)
        {
            Err = 1;
            return NULL;
        }
        opcode = *lex;
        if ( *lex == 7 || *lex == 8 )
        {
            left = newNode();
            left->opcode = opcode;
            if ( *lex == 7 ) left->fvalue = Fvalue;
            if ( *lex == 8 ) left->ivar = Ivar;
        }
        else
        {
            *lex = getLex();
            if ( *lex != 1 )
            {
                Err = 1;
                return NULL;
            }
            Bc++;
            left = newNode();
            left->left = getTree();
            left->opcode = opcode;
        }
    }
    *lex = getLex();
    if (*lex == 31)
    {
        node = newNode();
        node->left = left;
        node->opcode = *lex;
        *lex = getLex();
        node->right = getSingleOp(lex);
        left = node;
    }
    return left;
}

static ExprTree * getOp(int *lex)
{
    int opcode;
    ExprTree *left;
    ExprTree *right;
    ExprTree *node;
    int neg = 0;

    *lex = getLex();
    if (PrevLex == 0 || PrevLex == 1)
    {
        if ( *lex == 4 )
        {
            neg = 1;
            *lex = getLex();
        }
        else if ( *lex == 3) *lex = getLex();
    }
    left = getSingleOp(lex);
    while ( *lex == 5 || *lex == 6)
    {
        opcode = *lex;
        *lex = getLex();
        right = getSingleOp(lex);
        node = newNode();
        if (Err) return NULL;
        node->left = left;
        node->right = right;
        node->opcode = opcode;
        left = node;
    }
    if ( neg )
    {
        node = newNode();
        if (Err) return NULL;
        node->left = left;
        node->right = NULL;
        node->opcode = 9;
        left = node;
    }
    return left;
}

static ExprTree * getTree()
{
    int      lex;
    int      opcode;
    ExprTree *left;
    ExprTree *right;
    ExprTree *node;

    left = getOp(&lex);
    for (;;)
    {
        if ( lex == 0 || lex == 2 )
        {
            if ( lex == 2 ) Bc--;
            break;
        }
        if (lex != 3 && lex != 4 )
        {
            Err = 1;
            break;
        }
        opcode = lex;
        right = getOp(&lex);
        node = newNode();
        if (Err) break;
        node->left = left;
        node->right = right;
        node->opcode = opcode;
        left = node;
    }
    return left;
}

static void traverseTree(ExprTree *tree, MathExpr **expr)
{
    MathExpr *node;
    if ( tree == NULL) return;
    traverseTree(tree->left,  expr);
    traverseTree(tree->right, expr);
    node = (MathExpr *) malloc(sizeof(MathExpr));
    if (node)
    {
        node->fvalue = tree->fvalue;
        node->opcode = tree->opcode;
        node->ivar = tree->ivar;
        node->next = NULL;
        node->prev = (*expr);
    }
    if (*expr) (*expr)->next = node;
    (*expr) = node;
}

static void deleteTree(ExprTree *tree)
{
    if (tree)
    {
        if (tree->left)  deleteTree(tree->left);
        if (tree->right) deleteTree(tree->right);
        free(tree);
    }
}

double mathexpr_eval(MathExpr *expr, double (*getVariableValue) (int))
{
    double ExprStack[MAX_STACK_SIZE];
    MathExpr *node = expr;
    double r1, r2;
    int stackindex = 0;

    ExprStack[0] = 0.0;
    while(node != NULL && stackindex >= 0)
    {
        switch (node->opcode)
        {
            case 3:
                r1 = ExprStack[stackindex];
                stackindex--;
                if (stackindex < 0) break;
                r2 = ExprStack[stackindex];
                ExprStack[stackindex] = r2 + r1;
                break;
            case 4:
                r1 = ExprStack[stackindex];
                stackindex--;
                if (stackindex < 0) break;
                r2 = ExprStack[stackindex];
                ExprStack[stackindex] = r2 - r1;
                break;
            case 5:
                r1 = ExprStack[stackindex];
                stackindex--;
                if (stackindex < 0) break;
                r2 = ExprStack[stackindex];
                ExprStack[stackindex] = r2 * r1;
                break;
            case 6:
                r1 = ExprStack[stackindex];
                stackindex--;
                if (stackindex < 0) break;
                r2 = ExprStack[stackindex];
                ExprStack[stackindex] = r2 / r1;
                break;
            case 7:
                stackindex++;
                if (stackindex >= MAX_STACK_SIZE) break;
                ExprStack[stackindex] = node->fvalue;
                break;
            case 8:
                if (getVariableValue != NULL) r1 = getVariableValue(node->ivar);
                else r1 = 0.0;
                stackindex++;
                if (stackindex >= MAX_STACK_SIZE) break;
                ExprStack[stackindex] = r1;
                break;
            case 9:
                ExprStack[stackindex] = -ExprStack[stackindex];
                break;
            case 10:
                r1 = ExprStack[stackindex];
                r2 = cos(r1);
                ExprStack[stackindex] = r2;
                break;
            case 11:
                r1 = ExprStack[stackindex];
                r2 = sin(r1);
                ExprStack[stackindex] = r2;
                break;
            case 12:
                r1 = ExprStack[stackindex];
                r2 = tan(r1);
                ExprStack[stackindex] = r2;
                break;
            case 13:
                r1 = ExprStack[stackindex];
                if (r1 == 0.0) r2 = 0.0; else r2 = 1.0/tan(r1);
                ExprStack[stackindex] = r2;
                break;
            case 14:
                r1 = ExprStack[stackindex];
                r2 = fabs(r1);
                ExprStack[stackindex] = r2;
                break;
            case 15:
                r1 = ExprStack[stackindex];
                if (r1 < 0.0) r2 = -1.0; else if (r1 > 0.0) r2 = 1.0; else r2 = 0.0;
                ExprStack[stackindex] = r2;
                break;
            case 16:
                r1 = ExprStack[stackindex];
                if (r1 < 0.0) r2 = 0.0; else r2 = sqrt(r1);
                ExprStack[stackindex] = r2;
                break;
            case 17:
                r1 = ExprStack[stackindex];
                if (r1 <= 0) r2 = 0.0; else r2 = log(r1);
                ExprStack[stackindex] = r2;
                break;
            case 18:
                r1 = ExprStack[stackindex];
                r2 = exp(r1);
                ExprStack[stackindex] = r2;
                break;
            case 19:
                r1 = ExprStack[stackindex];
                r2 = asin(r1);
                ExprStack[stackindex] = r2;
                break;
            case 20:
                r1 = ExprStack[stackindex];
                r2 = acos(r1);
                ExprStack[stackindex] = r2;
                break;
            case 21:
                r1 = ExprStack[stackindex];
                r2 = atan(r1);
                ExprStack[stackindex] = r2;
                break;
            case 22:
                r1 = ExprStack[stackindex];
                r2 = 1.57079632679489661923 - atan(r1);
                ExprStack[stackindex] = r2;
                break;
            case 23:
                r1 = ExprStack[stackindex];
                r2 = (exp(r1)-exp(-r1))/2.0;
                ExprStack[stackindex] = r2;
                break;
            case 24:
                r1 = ExprStack[stackindex];
                r2 = (exp(r1)+exp(-r1))/2.0;
                ExprStack[stackindex] = r2;
                break;
            case 25:
                r1 = ExprStack[stackindex];
                r2 = (exp(r1)-exp(-r1))/(exp(r1)+exp(-r1));
                ExprStack[stackindex] = r2;
                break;
            case 26:
                r1 = ExprStack[stackindex];
                r2 = (exp(r1)+exp(-r1))/(exp(r1)-exp(-r1));
                ExprStack[stackindex] = r2;
                break;
            case 27:
                r1 = ExprStack[stackindex];
                if (r1 == 0.0) r2 = 0.0; else r2 = log10(r1);
                ExprStack[stackindex] = r2;
                break;
            case 28:
                r1 = ExprStack[stackindex];
                if (r1 <= 0.0) r2 = 0.0; else r2 = 1.0;
                ExprStack[stackindex] = r2;
                break;
            case 31:
                r1 = ExprStack[stackindex];
                stackindex--;
                if (stackindex < 0) break;
                r2 = ExprStack[stackindex];
                if (r2 <= 0.0) r2 = 0.0; else r2 = pow(r2, r1);
                ExprStack[stackindex] = r2;
                break;
        }
        node = node->next;
    }
    if (stackindex >= 0) return ExprStack[stackindex];
    else return 0.0;
}

void mathexpr_delete(MathExpr *expr)
{
    if (expr) mathexpr_delete(expr->next);
    free(expr);
}

MathExpr * mathexpr_create(char *formula, int (*getVar) (char *))
{
    ExprTree *tree;
    MathExpr *expr = NULL;
    MathExpr *result = NULL;
    getVariableIndex = getVar;
    Err = 0;
    PrevLex = 0;
    CurLex = 0;
    S = formula;
    Len = (int)strlen(S);
    Pos = 0;
    Bc = 0;
    tree = getTree();
    if (Bc == 0 && Err == 0)
    {
        traverseTree(tree, &expr);
        while (expr)
        {
            result = expr;
            expr = expr->prev;
        }
    }
    deleteTree(tree);
    return result;
}