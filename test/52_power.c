int printf(char *fmt, ...);

int power(int base, int exp)
{
    int result;
    result = 1;
    while (exp > 0)
    {
        result = result * base;
        exp = exp - 1;
    }
    return result;
}

int main()
{
    printf("%d\n", power(2, 10));
    return 0;
}
