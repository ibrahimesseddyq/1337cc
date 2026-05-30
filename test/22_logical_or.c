int printf(char *fmt, ...);

int main()
{
    printf("%d\n", 0 || 1);
    printf("%d\n", 0 || 0);
    return 0;
}
