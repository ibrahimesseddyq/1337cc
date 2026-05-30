int printf(char *fmt, ...);

int main()
{
    int x;
    x = 5;
    printf("%d\n", x > 3 ? 1 : 0);
    return 0;
}
