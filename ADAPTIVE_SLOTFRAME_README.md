# Slotframe Adaptativo com Q-Learning

## Implementação

O sistema agora possui **slotframe dinâmico** que se adapta automaticamente baseado no aprendizado por reforço (Q-Learning).

## Como Funciona

### 1. **Mapeamento Action → Tamanho do Slotframe**

```
Action 0   → Slotframe de 8 slots   (mínimo)
Action 50  → Slotframe de 54 slots  (médio)
Action 100 → Slotframe de 101 slots (máximo)
```

**Fórmula de mapeamento:**
```c
tamanho = 8 + (action × 93 / 100)
```

### 2. **Processo de Aprendizado**

```
┌──────────────────────────────────────────┐
│ 1. Q-Learning escolhe action (0-100)    │
├──────────────────────────────────────────┤
│ 2. Action é mapeada para tamanho        │
│    de slotframe (8-101 slots)            │
├──────────────────────────────────────────┤
│ 3. Slotframe é redimensionado            │
│    dinamicamente                         │
├──────────────────────────────────────────┤
│ 4. Sistema opera por 120 segundos        │
├──────────────────────────────────────────┤
│ 5. Reward é calculado baseado em:       │
│    • Transmissões (θ₁ × tx)              │
│    • Recepções (θ₁ × rx)                 │
│    • Buffer management (θ₂)              │
│    • Conflitos (penalidade)              │
├──────────────────────────────────────────┤
│ 6. Q-table é atualizada                  │
├──────────────────────────────────────────┤
│ 7. Repete o ciclo                        │
└──────────────────────────────────────────┘
```

### 3. **Função de Recompensa**

```c
reward = θ₁(tx + rx) - θ₂(buffer_penalty) - (100 × conflicts)
```

Onde:
- **θ₁ = 3.0**: peso para throughput
- **θ₂ = 1.5**: peso para gerenciamento de buffer
- **Conflitos**: penalidade de 100 por conflito

### 4. **Estratégia de Adaptação**

O Q-Learning aprende a **balancear**:

- **Slotframes pequenos (8-30 slots)**:
  - ✅ Menor consumo de energia
  - ✅ Menos overhead de sincronização
  - ❌ Menor vazão
  - ❌ Mais contenção entre nós

- **Slotframes médios (31-70 slots)**:
  - ✅ Equilíbrio energia/vazão
  - ✅ Boa capacidade para redes médias

- **Slotframes grandes (71-101 slots)**:
  - ✅ Máxima vazão
  - ✅ Menos contenção
  - ❌ Maior consumo de energia
  - ❌ Maior overhead

## Arquivos Modificados

1. **project-conf.h**
   - Define ranges: `TSCH_SCHEDULE_CONF_MIN_LENGTH = 8`
   - Define máximo: `TSCH_SCHEDULE_CONF_MAX_LENGTH = 101`

2. **node.c**
   - `adaptive_slotframe_resize()`: redimensiona slotframe dinamicamente
   - `set_up_new_schedule()`: mapeia action para tamanho
   - `current_slotframe_size`: rastreia tamanho atual

## Vantagens da Implementação

1. **Adaptabilidade**: ajusta-se automaticamente à carga da rede
2. **Otimização**: aprende o melhor tamanho para cada cenário
3. **Eficiência energética**: pode reduzir slots quando não há tráfego
4. **Escalabilidade**: suporta desde 2 até dezenas de nós
5. **Aprendizado distribuído**: integrado com federated learning

## Logs de Exemplo

```
[INFO: App] ============ Q-Learning Cycle Start ============
[INFO: App] Selected action: 45 (slotframe will be resized)
[INFO: App] Q-Learning action=45 maps to slotframe_size=50
[INFO: App] Resizing slotframe: 8 -> 50 slots
[INFO: TSCH Sched] Removing all slotframes
[INFO: TSCH Sched] Adding slotframe 0, size 50
[INFO: App] Slotframe resized successfully to 50 slots
[INFO: App] Buffer Size: before=2 after=0 current=0
[INFO: App] Chosen Action: 45, Current Slotframe Size: 50
[INFO: App] Reward: tx=15 rx=8 conflicts=0 reward=67.00
[INFO: App] ============ Q-Learning Cycle End ============
```

## Configuração

Para ajustar os limites do slotframe, edite em `project-conf.h`:

```c
#define TSCH_SCHEDULE_CONF_MIN_LENGTH 8    // mínimo
#define TSCH_SCHEDULE_CONF_MAX_LENGTH 101  // máximo
```

## Próximos Passos (Opcional)

- [ ] Implementar exploração ε-greedy para melhor exploração do espaço de ações
- [ ] Adicionar heurísticas para convergência mais rápida
- [ ] Implementar predição de carga para ajuste proativo
- [ ] Criar métricas de eficiência energética por tamanho de slotframe
