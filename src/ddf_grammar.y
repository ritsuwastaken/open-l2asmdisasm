%name DdfParse
%token_prefix TK_
%token_type {ddf::Token}
%extra_argument {ddf::State *state}
%syntax_error { state->syntax_error(yymajor, TOKEN); }
%stack_overflow { state->fail(0, "DDF nesting too deep"); }

%type value {ddf::Value}

ddf ::= globals block blocks.

blocks ::= .
blocks ::= blocks globals block.

block ::= open fields RBRACE.
open ::= LBRACE. { state->open_block(); }
group ::= LBRACE. { state->open_group(); }

globals ::= .
globals ::= globals WORD(N) EQ WORD(V) SEMI.   { state->global(N, V, false); }
globals ::= globals WORD(N) EQ STRING(V) SEMI. { state->global(N, V, true); }

fields ::= .
fields ::= fields WORD(T) WORD(N) SEMI.                         { state->declare(T, N, {}, 0); }
fields ::= fields WORD(T) WORD(N) LBRACKET WORD(C) RBRACKET SEMI. { state->declare(T, N, C, '['); }
fields ::= fields WORD(T) WORD(N) LBRACE WORD(C) RBRACE SEMI.   { state->declare(T, N, C, '{'); }
fields ::= fields group fields RBRACE WORD(N) LBRACKET WORD(C) RBRACKET SEMI. { state->close_group(N, C, true); }
fields ::= fields group fields RBRACE WORD(N) SEMI.                         { state->close_group(N, {}, false); }
fields ::= fields WORD(P) EQ WORD(V) SEMI.                      { state->property(P, V); }
fields ::= fields WORD(P) EQ LBRACKET items RBRACKET SEMI.      { state->list_property(P); }

items ::= item.
items ::= items COMMA item.
item ::= LPAREN value(A) COMMA value(B) RPAREN. { state->items.push_back({A, B}); }

value(V) ::= WORD(W).                { V = {W, W, 0}; }
value(V) ::= WORD(L) DOTDOT WORD(H). { V = {L, H, '.'}; }
value(V) ::= WORD(F) COLON WORD(M).  { V = {F, M, ':'}; }
