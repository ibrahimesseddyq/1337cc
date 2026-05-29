CXX      = g++
CXXFLAGS = -std=c++17 -fpermissive -Wall -Wextra -Wpedantic -g -I./

SRCS = cpp/main.cpp cpp/compiler.cpp cpp/cprocess.cpp cpp/token.cpp cpp/lex_process.cpp cpp/lexer.cpp \
       cpp/node.cpp cpp/scope.cpp cpp/symresolver.cpp cpp/datatype.cpp \
       cpp/array.cpp cpp/helper.cpp cpp/fixup.cpp cpp/expressionable.cpp cpp/codegen.cpp cpp/parser.cpp \
       cpp/helpers/vector.cpp cpp/helpers/buffer.cpp

OBJS = $(patsubst %.cpp, ./build/%.o, $(SRCS))

all: $(OBJS)
	$(CXX) $(CXXFLAGS) -o 1337cc $(OBJS)

./build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f 1337cc
	rm -rf ./build

re: clean all
