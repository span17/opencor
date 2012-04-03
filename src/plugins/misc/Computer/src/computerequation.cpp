//==============================================================================
// Computer equation class
//==============================================================================

#include "computerequation.h"
#include "computermath.h"

//==============================================================================

#include <cmath>

//==============================================================================

namespace OpenCOR {
namespace Computer {

//==============================================================================

ComputerEquation::ComputerEquation(ComputerEquation *pEquation)
{
    // Initialise the equation using pEquation

    initialiseFrom(pEquation);
}

//==============================================================================

ComputerEquation::ComputerEquation(const Type &pType,
                                   ComputerEquation *pLeft,
                                   ComputerEquation *pRight) :
    mType(pType),
    mParameterName(QString()),
    mParameterIndex(-1),
    mNumber(0),
    mLeft(pLeft),
    mRight(pRight)
{
}

//==============================================================================

ComputerEquation::ComputerEquation(const Type &pType,
                                   const int &pArgumentsCount,
                                   ComputerEquation **pArguments) :
    mType(pType),
    mParameterName(QString()),
    mParameterIndex(-1),
    mNumber(0),
    mLeft(pArguments[0]),
    mRight(0)
{
    // Initialise the left and right nodes based on pArguments

    ComputerEquation **crtRight = &mRight;

    for (int i = 1, iMax = pArgumentsCount-1; i <= iMax; ++i)
        if (i != iMax) {
            // We are not dealing with the last argument, so need to create a
            // new node

            *crtRight = new ComputerEquation(OtherArguments, pArguments[i]);

            crtRight = &(*crtRight)->mRight;
        } else {
            // We are dealing with the last argument, so...

            *crtRight = pArguments[i];
        }
}

//==============================================================================

ComputerEquation::ComputerEquation(const Type &pType,
                                   ComputerEquation *pLeftLeft,
                                   ComputerEquation *pLeftRight,
                                   ComputerEquation *pRightLeft,
                                   ComputerEquation *pRightRight) :
    mType(pType),
    mParameterName(QString()),
    mParameterIndex(-1),
    mNumber(0),
    mLeft(new ComputerEquation(TwoArguments, pLeftLeft, pLeftRight)),
    mRight(new ComputerEquation(TwoArguments, pRightLeft, pRightRight))
{
}

//==============================================================================

ComputerEquation::ComputerEquation(const QString &pParameterName) :
    mType(DirectParameter),
    mParameterName(pParameterName),
    mParameterIndex(-1),
    mNumber(0),
    mLeft(0),
    mRight(0)
{
}

//==============================================================================

ComputerEquation::ComputerEquation(const QString &pParameterName,
                                   const int &pParameterIndex) :
    mType(IndirectParameter),
    mParameterName(pParameterName),
    mParameterIndex(pParameterIndex),
    mNumber(0),
    mLeft(0),
    mRight(0)
{
}

//==============================================================================

ComputerEquation::ComputerEquation(const double &pNumber) :
    mType(Number),
    mParameterName(QString()),
    mParameterIndex(-1),
    mNumber(pNumber),
    mLeft(0),
    mRight(0)
{
}

//==============================================================================

ComputerEquation::~ComputerEquation()
{
    // Delete some internal objects

    delete mLeft;
    delete mRight;
}

//==============================================================================

ComputerEquation::Type ComputerEquation::type() const
{
    // Return the equation's type

    return mType;
}

//==============================================================================

QString ComputerEquation::typeAsString() const
{
    // Return the equation's type as a string
    // Note: this method is mainly used for debugging purposes, though who knows
    //       it may someday prove useful in some other cases...

    switch (mType) {
    case Unknown:
        return "Unknown";
    case DirectParameter:
        return "DirectParameter";
    case IndirectParameter:
        return "IndirectParameter";
    case Number:
        return "Number";
    case Times:
        return "Times";
    case Divide:
        return "Divide";
    case Modulo:
        return "Modulo";
    case Plus:
        return "Plus";
    case Minus:
        return "Minus";
    case Not:
        return "Not";
    case Or:
        return "Or";
    case Xor:
        return "Xor";
    case And:
        return "And";
    case EqualEqual:
        return "EqualEqual";
    case NotEqual:
        return "NotEqual";
    case LowerThan:
        return "LowerThan";
    case GreaterThan:
        return "GreaterThan";
    case LowerOrEqualThan:
        return "LowerOrEqualThan";
    case GreaterOrEqualThan:
        return "GreaterOrEqualThan";
    case Abs:
        return "Abs";
    case Exp:
        return "Exp";
    case Log:
        return "Log";
    case Ceil:
        return "Ceil";
    case Floor:
        return "Floor";
    case Sin:
        return "Sin";
    case Cos:
        return "Cos";
    case Tan:
        return "Tan";
    case Sinh:
        return "Sinh";
    case Cosh:
        return "Cosh";
    case Tanh:
        return "Tanh";
    case Asin:
        return "Asin";
    case Acos:
        return "Acos";
    case Atan:
        return "Atan";
    case Asinh:
        return "Asinh";
    case Acosh:
        return "Acosh";
    case Atanh:
        return "Atanh";
    case ArbitraryLog:
        return "ArbitraryLog";
    case Pow:
        return "Pow";
    case Quot:
        return "Quot";
    case Rem:
        return "Rem";
    case Gcd:
        return "Gcd";
    case Lcm:
        return "Lcm";
    case Max:
        return "Max";
    case Min:
        return "Min";
    case DefInt:
        return "DefInt";
    case Assign:
        return "Assign";
    case Piecewise:
        return "Piecewise";
    case PiecewiseCases:
        return "PiecewiseCases";
    case OtherArguments:
        return "OtherArguments";
    case TwoArguments:
        return "TwoArguments";
    default:
        return "???";
    }
}

//==============================================================================

QString ComputerEquation::parameterName() const
{
    // Return the equation's parameter name

    return mParameterName;
}

//==============================================================================

int ComputerEquation::parameterIndex() const
{
    // Return the equation's parameter index

    return mParameterIndex;
}

//==============================================================================

double ComputerEquation::number() const
{
    // Return the equation's number

    return mNumber;
}

//==============================================================================

ComputerEquation * ComputerEquation::left() const
{
    // Return the equation's left branch

    return mLeft;
}

//==============================================================================

ComputerEquation * ComputerEquation::right() const
{
    // Return the equation's right branch

    return mRight;
}

//==============================================================================

void ComputerEquation::initialiseFrom(ComputerEquation *pEquation)
{
    // Initialise the equation using pEquation, if possible

    mType = pEquation?pEquation->type():Unknown;

    mParameterName  = pEquation?pEquation->parameterName():QString();
    mParameterIndex = pEquation?pEquation->parameterIndex():-1;

    mNumber = pEquation?pEquation->number():0;

    if (pEquation && pEquation->left()) {
        mLeft = new ComputerEquation();

        mLeft->initialiseFrom(pEquation->left());
    } else {
        mLeft = 0;
    }

    if (pEquation && pEquation->right()) {
        mRight = new ComputerEquation();

        mRight->initialiseFrom(pEquation->right());
    } else {
        mRight = 0;
    }
}

//==============================================================================

bool ComputerEquation::numberArguments(ComputerEquation *pArguments)
{
    // Return whether all of the arguments (i.e. including any children nodes of
    // pArguments, if applicable) are numbers

    if (pArguments->left()->type() != Number)
        // The left node is not a number, so...

        return false;
    else if (pArguments->right()->type() == Number)
        // The right node is a number and so is the left node, so...

        return true;
    else if (pArguments->right()->type() == OtherArguments)
        // The right node contains other arguments and the left node is a
        // number, so...

        return numberArguments(pArguments->right());
    else
        // The right node neither is a number nor consists of other operands,
        // so...

        return false;
}

//==============================================================================

double ComputerEquation::gcd(ComputerEquation *pLeftNode,
                             ComputerEquation *pRightNode)
{
    // Return the greatest common divisor of the two nodes

    if (pRightNode->type() == Number)
        return ::gcd(2, pLeftNode->number(), pRightNode->number());
    else
        return ::gcd(2, pLeftNode->number(),
                        gcd(pRightNode->left(), pRightNode->right()));
}

//==============================================================================

double ComputerEquation::lcm(ComputerEquation *pLeftNode,
                             ComputerEquation *pRightNode)
{
    // Return the lowest common multiplicator of the two nodes

    if (pRightNode->type() == Number)
        return ::lcm(2, pLeftNode->number(), pRightNode->number());
    else
        return ::lcm(2, pLeftNode->number(),
                        lcm(pRightNode->left(), pRightNode->right()));
}

//==============================================================================

double ComputerEquation::max(ComputerEquation *pLeftNode,
                             ComputerEquation *pRightNode)
{
    // Return the maximum of the two nodes

    if (pRightNode->type() == Number)
        return ::max(2, pLeftNode->number(), pRightNode->number());
    else
        return ::max(2, pLeftNode->number(),
                        max(pRightNode->left(), pRightNode->right()));
}

//==============================================================================

double ComputerEquation::min(ComputerEquation *pLeftNode,
                             ComputerEquation *pRightNode)
{
    // Return the minimum of the two nodes

    if (pRightNode->type() == Number)
        return ::min(2, pLeftNode->number(), pRightNode->number());
    else
        return ::min(2, pLeftNode->number(),
                        min(pRightNode->left(), pRightNode->right()));
}


//==============================================================================

void ComputerEquation::simplify()
{
    // Simplify the equation starting from its top node

    simplifyNode(this);
}

//==============================================================================

void ComputerEquation::simplifyNode(ComputerEquation *pNode)
{
    // Make sure that the node is valid

    if (!pNode)
        // It isn't, so...

        return;

    // Simplify both the left and right nodes

    simplifyNode(pNode->left());
    simplifyNode(pNode->right());

    // Simplify the current node based on the contents of its left and right
    // nodes

    switch (pNode->type()) {
    // Mathematical operators

    case Times:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1*N2

            replaceNodeWithNumber(pNode, pNode->left()->number()*pNode->right()->number());
        else if ((pNode->left()->type() == Number) && (pNode->left()->number() == 1))
            // 1*X ---> X

            replaceNodeWithChildNode(pNode, pNode->right());
        else if ((pNode->right()->type() == Number) && (pNode->right()->number() == 1))
            // X*1 ---> X

            replaceNodeWithChildNode(pNode, pNode->left());

        break;
    case Divide:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1/N2

            replaceNodeWithNumber(pNode, pNode->left()->number()/pNode->right()->number());
        else if ((pNode->right()->type() == Number) && (pNode->right()->number() == 1))
            // X/1 ---> X

            replaceNodeWithChildNode(pNode, pNode->left());

        break;
    case Modulo:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // fmod(N1, N2)

            replaceNodeWithNumber(pNode, std::fmod(pNode->left()->number(), pNode->right()->number()));

        break;
    case Plus:
        if (!pNode->right())
            // +X ---> X

            replaceNodeWithChildNode(pNode, pNode->left());
        else if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1+N2

            replaceNodeWithNumber(pNode, pNode->left()->number()+pNode->right()->number());
        else if ((pNode->left()->type() == Number) && (pNode->left()->number() == 0))
            // 0+X ---> X

            replaceNodeWithChildNode(pNode, pNode->right());
        else if ((pNode->right()->type() == Number) && (pNode->right()->number() == 0))
            // X+0 ---> X

            replaceNodeWithChildNode(pNode, pNode->left());

        break;
    case Minus:
        if (!pNode->right()) {
            if (pNode->left()->type() == Number)
                // -N

                replaceNodeWithNumber(pNode, -pNode->left()->number());
        } else if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number)) {
            // N1-N2

            replaceNodeWithNumber(pNode, pNode->left()->number()-pNode->right()->number());
        } else if ((pNode->right()->type() == Number) && (pNode->right()->number() == 0)) {
            // X-0 ---> X

            replaceNodeWithChildNode(pNode, pNode->left());
        }

        break;

    // Logical operators

    case Not:
        if (pNode->left()->type() == Number)
            // !N

            replaceNodeWithNumber(pNode, !pNode->left()->number());

        break;
    case Or:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 || N2

            replaceNodeWithNumber(pNode, pNode->left()->number() || pNode->right()->number());

        break;
    case Xor:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 ^ N2

            replaceNodeWithNumber(pNode, (pNode->left()->number() != 0) ^ (pNode->right()->number() != 0));

        break;
    case And:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 && N2

            replaceNodeWithNumber(pNode, pNode->left()->number() && pNode->right()->number());

        break;
    case EqualEqual:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 == N2

            replaceNodeWithNumber(pNode, pNode->left()->number() == pNode->right()->number());

        break;
    case NotEqual:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 != N2

            replaceNodeWithNumber(pNode, pNode->left()->number() != pNode->right()->number());

        break;
    case LowerThan:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 < N2

            replaceNodeWithNumber(pNode, pNode->left()->number() < pNode->right()->number());

        break;
    case GreaterThan:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 > N2

            replaceNodeWithNumber(pNode, pNode->left()->number() > pNode->right()->number());

        break;
    case LowerOrEqualThan:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 <= N2

            replaceNodeWithNumber(pNode, pNode->left()->number() <= pNode->right()->number());

        break;
    case GreaterOrEqualThan:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // N1 >= N2

            replaceNodeWithNumber(pNode, pNode->left()->number() >= pNode->right()->number());

        break;

    // Mathematical functions with 1 argument

    case Abs:
        if (pNode->left()->type() == Number)
            // fabs(N)

            replaceNodeWithNumber(pNode, std::fabs(pNode->left()->number()));

        break;
    case Exp:
        if (pNode->left()->type() == Number)
            // exp(N)

            replaceNodeWithNumber(pNode, std::exp(pNode->left()->number()));

        break;
    case Log:
        if (pNode->left()->type() == Number)
            // log(N)

            replaceNodeWithNumber(pNode, std::log(pNode->left()->number()));

        break;
    case Ceil:
        if (pNode->left()->type() == Number)
            // ceil(N)

            replaceNodeWithNumber(pNode, std::ceil(pNode->left()->number()));

        break;
    case Floor:
        if (pNode->left()->type() == Number)
            // floor(N)

            replaceNodeWithNumber(pNode, std::floor(pNode->left()->number()));

        break;
    case Factorial:
        if (pNode->left()->type() == Number)
            // N!

            replaceNodeWithNumber(pNode, factorial(pNode->left()->number()));

        break;
    case Sin:
        if (pNode->left()->type() == Number)
            // sin(N)

            replaceNodeWithNumber(pNode, std::sin(pNode->left()->number()));

        break;
    case Cos:
        if (pNode->left()->type() == Number)
            // cos(N)

            replaceNodeWithNumber(pNode, std::cos(pNode->left()->number()));

        break;
    case Tan:
        if (pNode->left()->type() == Number)
            // tan(N)

            replaceNodeWithNumber(pNode, std::tan(pNode->left()->number()));

        break;
    case Sinh:
        if (pNode->left()->type() == Number)
            // sinh(N)

            replaceNodeWithNumber(pNode, std::sinh(pNode->left()->number()));

        break;
    case Cosh:
        if (pNode->left()->type() == Number)
            // cosh(N)

            replaceNodeWithNumber(pNode, std::cosh(pNode->left()->number()));

        break;
    case Tanh:
        if (pNode->left()->type() == Number)
            // tanh(N)

            replaceNodeWithNumber(pNode, std::tanh(pNode->left()->number()));

        break;
    case Asin:
        if (pNode->left()->type() == Number)
            // asin(N)

            replaceNodeWithNumber(pNode, std::asin(pNode->left()->number()));

        break;
    case Acos:
        if (pNode->left()->type() == Number)
            // acos(N)

            replaceNodeWithNumber(pNode, std::acos(pNode->left()->number()));

        break;
    case Atan:
        if (pNode->left()->type() == Number)
            // atan(N)

            replaceNodeWithNumber(pNode, std::atan(pNode->left()->number()));

        break;
    case Asinh:
        if (pNode->left()->type() == Number)
            // asinh(N)

            replaceNodeWithNumber(pNode, asinh(pNode->left()->number()));

        break;
    case Acosh:
        if (pNode->left()->type() == Number)
            // acosh(N)

            replaceNodeWithNumber(pNode, acosh(pNode->left()->number()));

        break;
    case Atanh:
        if (pNode->left()->type() == Number)
            // atanh(N)

            replaceNodeWithNumber(pNode, atanh(pNode->left()->number()));

        break;

    // Mathematical functions with 2 arguments

    case ArbitraryLog:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // arbitraryLog(N1, N2)

            replaceNodeWithNumber(pNode, arbitraryLog(pNode->left()->number(), pNode->right()->number()));

        break;
    case Pow:
        // Note: we could support further simplifications (well, optimisations)
        //       such as pow(x, 2) = x*x, but well... maybe someday...

        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // pow(N1, N2)

            replaceNodeWithNumber(pNode, std::pow(pNode->left()->number(), pNode->right()->number()));

        break;
    case Quot:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // quot(N1, N2)

            replaceNodeWithNumber(pNode, quot(pNode->left()->number(), pNode->right()->number()));

        break;
    case Rem:
        if ((pNode->left()->type() == Number) && (pNode->right()->type() == Number))
            // fmod(N1, N2)

            replaceNodeWithNumber(pNode, fmod(pNode->left()->number(), pNode->right()->number()));

        break;

    // Mathematical functions with 2+ arguments

    case Gcd:
        if (numberArguments(pNode))
            // gcd(N1, N2, ...)

            replaceNodeWithNumber(pNode, gcd(pNode->left(), pNode->right()));

        break;
    case Lcm:
        if (numberArguments(pNode))
            // lcm(N1, N2, ...)

            replaceNodeWithNumber(pNode, lcm(pNode->left(), pNode->right()));

        break;
    case Max:
        if (numberArguments(pNode))
            // max(N1, N2, ...)

            replaceNodeWithNumber(pNode, max(pNode->left(), pNode->right()));

        break;
    case Min:
        if (numberArguments(pNode))
            // min(N1, N2, ...)

            replaceNodeWithNumber(pNode, min(pNode->left(), pNode->right()));

        break;
    }
}

//==============================================================================

void ComputerEquation::replaceNodeWithNumber(ComputerEquation *pNode,
                                             const double &pNumber)
{
    // Replace the given node with a number

    pNode->mType = Number;

    pNode->mNumber = pNumber;

    delete pNode->mLeft;
    delete pNode->mRight;

    pNode->mLeft  = 0;
    pNode->mRight = 0;
}

//==============================================================================

void ComputerEquation::replaceNodeWithChildNode(ComputerEquation *pNode,
                                                ComputerEquation *pChildNode)
{
    // Replace the given node with one of its child nodes

    // Keep track of the node's child nodes

    ComputerEquation *leftChildNode  = pNode->left();
    ComputerEquation *rightChildNode = pNode->right();

    // Make sure that pChildNode is indeed one of the node's child nodes

    if ((pChildNode != leftChildNode) && (pChildNode != rightChildNode))
        // pChildNode is not one of the node's child nodes, so...

        return;

    // Initialise the node using its child node

    pNode->initialiseFrom(pChildNode);

    // Delete the child nodes now that they are not needed anymore

    delete leftChildNode;
    delete rightChildNode;
}

//==============================================================================

}   // namespace Computer
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
