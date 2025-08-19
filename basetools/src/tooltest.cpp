#include "carray.h"
#include "clist.h"
#include "cstring.h"
#include "cavltree.h"

int intCompare(int const & i, int const & j) {
    return (i < j) ? -1 : (i > j) ? 1 : 0;
}

int strCompare(char* const & i, char* const & j) {
    return strcmp(i, j);
}


bool NodeProcessor(int const & data) {
    int x = data;
    return true;
}

int main() {
    int x;
    CString s;
    s= "a ; b ; c ; d ; e ; f";
    //CString t = s.Replace("; ", "=").Replace(" =", "=");
    CList<CString> values = s.Split(';');
    /*
    CArray<int> a;
    CList<int> l;
    CAvlTree<int, int> t; 
    t.SetComparator(intCompare);
    t.Insert(9,9);
    t.Insert(5,5);
    t.Insert(7,7);
    t.Insert(2,2);
    t.Insert(1,1);
    t.Insert(3,3);
    t.Insert(8,8);
    t.Insert(4,4);
    t.Insert(6,6);
    t.Insert(0,0);
    t.Walk(NodeProcessor);
    l.Append(1);
    l.Append(2);
    l.Append(3);
    l.Append(4);
    l.Append(5);
    x = l[3];
    //    for (auto const& e : l)
    //        x = *e;
    for (const auto [i, p] : l)
        x = *p;
    */
    char* ps;
//    CString v;
    for (const auto [i, p] : values) {
        CString v = *p;
        ps = (char*)v;
    }
    x = 0;
}
