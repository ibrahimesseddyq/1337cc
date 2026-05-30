int printf(char *fmt, ...);

int double_val(int x)
{
    return x * 2;
}

int triple_val(int x)
{
    return x * 3;
}

int combine(int x)
{
    return double_val(x) + triple_val(x);
}

int main()
{
    printf("%d\n", combine(4));
    return 0;
}
