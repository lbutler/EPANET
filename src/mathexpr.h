#ifndef MATHEXPR_H
#define MATHEXPR_H

struct ExprNode
{
    int    opcode;
    int    ivar;
    double fvalue;
    struct ExprNode *prev;
    struct ExprNode *next;
};
typedef struct ExprNode MathExpr;

MathExpr* mathexpr_create(char* s, int (*getVar) (char *));
double    mathexpr_eval(MathExpr* expr, double (*getVal) (int));
void      mathexpr_delete(MathExpr* expr);

#endif // MATHEXPR_H