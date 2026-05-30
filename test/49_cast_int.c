int printf(char *fmt, ...);

int truncate_div(int a, int b)
{
    return a / b;
}

int main()
{
    printf("%d\n", truncate_div(37, 10));
    return 0;
}
