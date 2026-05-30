int printf(char *fmt, ...);

int main()
{
    printf("%d\n", 1 && 1);
    printf("%d\n", 1 && 0);
    return 0;
}
